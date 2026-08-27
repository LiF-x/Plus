// lifxd — LiFx dispatcher daemon. Issue #47.
//
// Chunk 1 (#70): TCP listener + hello/ack one-shot.
// Chunk 2 (#72): MySQL-backed world registration.
// Chunk 3 (#74): DLL-side client.
// Chunk 4 (#76): typed-message protocol with persistent connection +
// heartbeat.
// Chunk 5 (this PR, #78): peer-to-peer event forwarding. The daemon
// owns a registry of live peer writers and routes opaque payloads from
// one peer to another via base64-encoded JSON frames.
//
// Wire format unchanged: [u32 LE length] [JSON].
//
// Frames (client -> daemon):
//   { "type":"hello", "world_id":<int>, "server_uuid":"<uuid>", "proto_version":<int> }
//   { "type":"ping",  "ts":<unix_ms> }
//   { "type":"forward", "target_peer_id":"<uuid>", "payload_b64":"<base64>" }
//
// Frames (daemon -> client):
//   { "type":"hello_ack", "daemon_proto":1, "assigned_peer_id":"<uuid>",
//     "world_id":<int>, "first_registration":<bool> }
//   { "type":"pong", "ts":<unix_ms> }
//   { "type":"delivery", "from_peer_id":"<uuid>", "payload_b64":"<base64>" }
//   { "type":"forward_error", "target_peer_id":"<uuid>",
//     "reason":"unknown_peer"|"self_route_refused"|"target_buffer_full" }

use anyhow::{Context, Result, anyhow, bail};
use base64::{Engine as _, engine::general_purpose::STANDARD as B64};
use serde::{Deserialize, Serialize};
use sqlx::mysql::{MySqlPool, MySqlPoolOptions};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::tcp::{OwnedReadHalf, OwnedWriteHalf};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::{Mutex, mpsc};
use tracing::{info, warn};

const DEFAULT_BIND: &str = "0.0.0.0:7400";
const DAEMON_PROTO_VERSION: u32 = 1;
const MAX_FRAME_BYTES: usize = 64 * 1024;
const WRITER_QUEUE_DEPTH: usize = 64;

const TABLE_SCHEMA: &str = r#"
CREATE TABLE IF NOT EXISTS dispatcher_worlds (
    peer_id        CHAR(36)     PRIMARY KEY,
    world_id       INT UNSIGNED NOT NULL,
    server_uuid    CHAR(36)     NOT NULL,
    registered_at  TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_seen_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uq_server_uuid (server_uuid)
)
"#;

// Sector ownership map — chunk 10a / issue #89. Peers claim
// world_sector_ids they serve; the dispatcher answers `resolve_sector`
// queries with the current owner. Idempotent upsert on claim. No TTL
// yet: a peer disconnecting does NOT lose its claims (handoff would
// still try to forward, get unknown_peer back, and the caller can
// re-resolve). TTL/heartbeat is a later chunk.
const SECTORS_SCHEMA: &str = r#"
CREATE TABLE IF NOT EXISTS dispatcher_sectors (
    world_sector_id INT UNSIGNED PRIMARY KEY,
    peer_id         CHAR(36)     NOT NULL,
    claimed_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_peer (peer_id)
)
"#;

// === Frame types =============================================================

#[derive(Debug, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
enum InboundFrame {
    Hello {
        world_id: u32,
        server_uuid: String,
        #[serde(default)]
        #[allow(dead_code)]
        proto_version: u32,
    },
    Ping {
        #[serde(default)]
        ts: u64,
    },
    Forward {
        target_peer_id: String,
        payload_b64: String,
    },
    // Sector ownership routing (chunk 10a). claim is idempotent upsert;
    // resolve returns the current owner or sector_unknown.
    ClaimSector {
        world_sector_id: u32,
    },
    ResolveSector {
        world_sector_id: u32,
    },
}

#[derive(Debug, Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
enum OutboundFrame {
    HelloAck {
        daemon_proto: u32,
        assigned_peer_id: String,
        world_id: u32,
        first_registration: bool,
    },
    Pong {
        ts: u64,
    },
    Delivery {
        from_peer_id: String,
        payload_b64: String,
    },
    ForwardError {
        target_peer_id: String,
        reason: &'static str,
    },
    SectorClaimed {
        world_sector_id: u32,
        peer_id: String,
    },
    SectorResolved {
        world_sector_id: u32,
        peer_id: String,
    },
    SectorUnknown {
        world_sector_id: u32,
    },
}

// === Per-peer registry =======================================================
//
// Each live connection has an mpsc sender owned here. Other peers' forward
// requests push Delivery frames in; the connection's own writer task drains.
// Drop the entry on disconnect — the writer task ends naturally when its
// receiver returns None.
#[derive(Default)]
struct Registry {
    peers: Mutex<HashMap<String, mpsc::Sender<OutboundFrame>>>,
}

impl Registry {
    async fn insert(&self, peer_id: String, tx: mpsc::Sender<OutboundFrame>) {
        self.peers.lock().await.insert(peer_id, tx);
    }
    async fn remove(&self, peer_id: &str) {
        self.peers.lock().await.remove(peer_id);
    }
    // Returns the target's sender if present. We clone (cheap — mpsc::Sender
    // is an Arc) so the caller can push without holding the registry lock.
    async fn sender_for(&self, peer_id: &str) -> Option<mpsc::Sender<OutboundFrame>> {
        self.peers.lock().await.get(peer_id).cloned()
    }
}

// === Shared state ============================================================

struct State {
    pool: MySqlPool,
    registry: Registry,
}

// === Main ====================================================================

#[tokio::main]
async fn main() -> Result<()> {
    let log_filter = std::env::var("LIFXD_LOG").unwrap_or_else(|_| "info".into());
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::new(log_filter))
        .init();

    let db_url = std::env::var("LIFXD_MYSQL_URL")
        .context("LIFXD_MYSQL_URL must be set, e.g. mysql://user:pass@host/db")?;
    let pool = MySqlPoolOptions::new()
        .max_connections(8)
        .connect(&db_url)
        .await
        .with_context(|| format!("connecting to MySQL at {db_url}"))?;
    sqlx::query("SELECT 1").execute(&pool).await.context("MySQL connectivity check failed")?;
    sqlx::query(TABLE_SCHEMA).execute(&pool).await.context("creating dispatcher_worlds table")?;
    sqlx::query(SECTORS_SCHEMA).execute(&pool).await.context("creating dispatcher_sectors table")?;
    info!("MySQL connected, dispatcher_worlds ready");

    let bind = std::env::var("LIFXD_BIND").unwrap_or_else(|_| DEFAULT_BIND.into());
    let listener = TcpListener::bind(&bind).await.with_context(|| format!("binding {bind}"))?;
    info!(%bind, "lifxd listening");

    let state = Arc::new(State { pool, registry: Registry::default() });
    let mut accept = std::pin::pin!(accept_loop(listener, state));
    let mut sigint = std::pin::pin!(tokio::signal::ctrl_c());
    tokio::select! {
        r = &mut accept => { r?; }
        r = &mut sigint => {
            r.context("waiting for ctrl-c")?;
            info!("shutdown signal received, exiting");
        }
    }
    Ok(())
}

async fn accept_loop(listener: TcpListener, state: Arc<State>) -> Result<()> {
    loop {
        let (sock, peer) = listener.accept().await.context("listener accept")?;
        let st = state.clone();
        tokio::spawn(async move {
            if let Err(e) = handle_peer(sock, peer, st).await {
                warn!(%peer, error = ?e, "peer handler ended");
            }
        });
    }
}

// === Per-peer pipeline =======================================================
//
// 1. Split socket into read/write halves.
// 2. Read the hello off the read half synchronously (need it before we can
//    register in the registry).
// 3. Insert our writer mpsc into the registry under the assigned peer_id.
// 4. Spawn writer task draining the mpsc -> write half.
// 5. Reader loop on the read half: dispatch ping/forward, push responses
//    onto our own writer mpsc (or someone else's, for forward).
// 6. On reader exit (disconnect/error), remove ourselves from the registry;
//    dropping our mpsc::Sender lets the writer task drain and exit cleanly.
async fn handle_peer(sock: TcpStream, peer: SocketAddr, state: Arc<State>) -> Result<()> {
    info!(%peer, "accepted connection");
    let (mut rd, wr) = sock.into_split();

    // Hello must be first.
    let frame = read_frame(&mut rd).await.context("reading hello frame")?;
    let first: InboundFrame = serde_json::from_slice(&frame).context("parsing first frame as JSON")?;
    let (world_id, server_uuid) = match first {
        InboundFrame::Hello { world_id, server_uuid, .. } => (world_id, server_uuid),
        InboundFrame::Ping { .. }    => bail!("first frame was ping, expected hello"),
        InboundFrame::Forward { .. } => bail!("first frame was forward, expected hello"),
        InboundFrame::ClaimSector { .. } | InboundFrame::ResolveSector { .. } => {
            bail!("first frame was sector op, expected hello");
        }
    };
    if server_uuid.trim().is_empty() {
        bail!("hello has empty server_uuid");
    }
    let (peer_id, first_registration) = upsert_world(&state.pool, world_id, &server_uuid).await?;
    info!(%peer, %peer_id, world_id, first_registration, "hello accepted");

    let (tx, rx) = mpsc::channel::<OutboundFrame>(WRITER_QUEUE_DEPTH);
    state.registry.insert(peer_id.clone(), tx.clone()).await;

    // Spawn the writer.
    let writer_handle = tokio::spawn(writer_loop(wr, rx));

    // hello_ack via the writer.
    if tx
        .send(OutboundFrame::HelloAck {
            daemon_proto: DAEMON_PROTO_VERSION,
            assigned_peer_id: peer_id.clone(),
            world_id,
            first_registration,
        })
        .await
        .is_err()
    {
        warn!(%peer, %peer_id, "writer mpsc closed during hello_ack");
    }

    // Reader loop.
    let reader_result = reader_loop(&mut rd, &state, &peer_id, &tx).await;
    if let Err(e) = reader_result {
        info!(%peer, %peer_id, error = ?e, "peer reader ended");
    }

    // Cleanup: drop our entry from the registry, drop our local tx clone so
    // the writer's mpsc::Receiver returns None and the task ends.
    state.registry.remove(&peer_id).await;
    drop(tx);
    let _ = writer_handle.await;
    info!(%peer, %peer_id, "session closed");
    Ok(())
}

async fn reader_loop(
    rd: &mut OwnedReadHalf,
    state: &Arc<State>,
    self_peer_id: &str,
    self_tx: &mpsc::Sender<OutboundFrame>,
) -> Result<()> {
    loop {
        let frame = read_frame(rd).await?;
        let inbound: InboundFrame = serde_json::from_slice(&frame).context("parsing frame as JSON")?;
        match inbound {
            InboundFrame::Hello { .. } => bail!("unexpected hello mid-session"),
            InboundFrame::Ping { ts } => {
                touch_peer(&state.pool, self_peer_id).await?;
                let _ = self_tx.send(OutboundFrame::Pong { ts }).await;
                info!(%self_peer_id, ts, "ping received -> pong sent");
            }
            InboundFrame::Forward { target_peer_id, payload_b64 } => {
                forward(state, self_peer_id, self_tx, target_peer_id, payload_b64).await?;
            }
            InboundFrame::ClaimSector { world_sector_id } => {
                claim_sector(&state.pool, world_sector_id, self_peer_id).await?;
                let _ = self_tx
                    .send(OutboundFrame::SectorClaimed {
                        world_sector_id,
                        peer_id: self_peer_id.to_string(),
                    })
                    .await;
                info!(%self_peer_id, world_sector_id, "sector claimed");
            }
            InboundFrame::ResolveSector { world_sector_id } => {
                let reply = match resolve_sector(&state.pool, world_sector_id).await? {
                    Some(peer_id) => OutboundFrame::SectorResolved { world_sector_id, peer_id },
                    None          => OutboundFrame::SectorUnknown   { world_sector_id },
                };
                let _ = self_tx.send(reply).await;
            }
        }
    }
}

// Route a forward request. Errors are reported back to the sender as a
// forward_error frame on its own writer mpsc — not propagated, since one
// bad route shouldn't kill the session.
async fn forward(
    state: &Arc<State>,
    from_peer_id: &str,
    from_tx: &mpsc::Sender<OutboundFrame>,
    target_peer_id: String,
    payload_b64: String,
) -> Result<()> {
    if target_peer_id == from_peer_id {
        let _ = from_tx
            .send(OutboundFrame::ForwardError {
                target_peer_id,
                reason: "self_route_refused",
            })
            .await;
        return Ok(());
    }
    // Cheap validation: we don't decode the payload here (daemon doesn't
    // care about contents), but we sanity-check it's valid base64 so
    // garbage stops at us instead of waking the target.
    if B64.decode(payload_b64.as_bytes()).is_err() {
        let _ = from_tx
            .send(OutboundFrame::ForwardError {
                target_peer_id,
                reason: "unknown_peer", // close enough; "bad_payload" isn't in the public reason list
            })
            .await;
        return Ok(());
    }
    let Some(target_tx) = state.registry.sender_for(&target_peer_id).await else {
        let _ = from_tx
            .send(OutboundFrame::ForwardError {
                target_peer_id,
                reason: "unknown_peer",
            })
            .await;
        return Ok(());
    };
    let delivery = OutboundFrame::Delivery {
        from_peer_id: from_peer_id.to_string(),
        payload_b64,
    };
    // Bounded try_send — surfaces back-pressure as an error rather than
    // blocking the reader (which would block all pings on this socket).
    if let Err(e) = target_tx.try_send(delivery) {
        let reason = match e {
            mpsc::error::TrySendError::Full(_)   => "target_buffer_full",
            mpsc::error::TrySendError::Closed(_) => "unknown_peer",
        };
        let _ = from_tx
            .send(OutboundFrame::ForwardError { target_peer_id, reason })
            .await;
    } else {
        info!(%from_peer_id, %target_peer_id, "forwarded");
    }
    Ok(())
}

async fn writer_loop(mut wr: OwnedWriteHalf, mut rx: mpsc::Receiver<OutboundFrame>) {
    while let Some(frame) = rx.recv().await {
        if let Err(e) = write_frame_json(&mut wr, &frame).await {
            warn!(error = ?e, "writer task dropping due to socket error");
            break;
        }
    }
    let _ = wr.shutdown().await;
}

// === DB helpers ==============================================================

async fn upsert_world(pool: &MySqlPool, world_id: u32, server_uuid: &str) -> Result<(String, bool)> {
    let mut tx = pool.begin().await.context("begin tx")?;
    let existing: Option<(String,)> = sqlx::query_as(
        "SELECT peer_id FROM dispatcher_worlds WHERE server_uuid = ? FOR UPDATE",
    )
    .bind(server_uuid)
    .fetch_optional(&mut *tx)
    .await
    .context("selecting existing peer")?;

    let (peer_id, first) = match existing {
        Some((id,)) => {
            sqlx::query("UPDATE dispatcher_worlds SET world_id = ? WHERE peer_id = ?")
                .bind(world_id)
                .bind(&id)
                .execute(&mut *tx)
                .await
                .context("touching existing peer")?;
            (id, false)
        }
        None => {
            let new_id = uuid::Uuid::new_v4().to_string();
            sqlx::query(
                "INSERT INTO dispatcher_worlds (peer_id, world_id, server_uuid) VALUES (?, ?, ?)",
            )
            .bind(&new_id)
            .bind(world_id)
            .bind(server_uuid)
            .execute(&mut *tx)
            .await
            .context("inserting new peer")?;
            (new_id, true)
        }
    };
    tx.commit().await.context("commit tx")?;
    Ok((peer_id, first))
}

async fn touch_peer(pool: &MySqlPool, peer_id: &str) -> Result<()> {
    sqlx::query("UPDATE dispatcher_worlds SET last_seen_at = NOW() WHERE peer_id = ?")
        .bind(peer_id)
        .execute(pool)
        .await
        .context("touching peer last_seen_at")?;
    Ok(())
}

async fn claim_sector(pool: &MySqlPool, world_sector_id: u32, peer_id: &str) -> Result<()> {
    sqlx::query(
        "INSERT INTO dispatcher_sectors (world_sector_id, peer_id) VALUES (?, ?) \
         ON DUPLICATE KEY UPDATE peer_id = VALUES(peer_id)",
    )
    .bind(world_sector_id)
    .bind(peer_id)
    .execute(pool)
    .await
    .context("upserting sector claim")?;
    Ok(())
}

async fn resolve_sector(pool: &MySqlPool, world_sector_id: u32) -> Result<Option<String>> {
    let row: Option<(String,)> = sqlx::query_as(
        "SELECT peer_id FROM dispatcher_sectors WHERE world_sector_id = ?",
    )
    .bind(world_sector_id)
    .fetch_optional(pool)
    .await
    .context("querying sector owner")?;
    Ok(row.map(|(p,)| p))
}

// === Frame I/O ===============================================================

async fn read_frame(rd: &mut OwnedReadHalf) -> Result<Vec<u8>> {
    let mut len = [0u8; 4];
    rd.read_exact(&mut len).await.context("reading length prefix")?;
    let n = u32::from_le_bytes(len) as usize;
    if n == 0 {
        bail!("zero-length frame");
    }
    if n > MAX_FRAME_BYTES {
        return Err(anyhow!("frame too large: {n} bytes > {MAX_FRAME_BYTES}"));
    }
    let mut buf = vec![0u8; n];
    rd.read_exact(&mut buf).await.context("reading frame body")?;
    Ok(buf)
}

async fn write_frame_json<T: Serialize>(wr: &mut OwnedWriteHalf, payload: &T) -> Result<()> {
    let bytes = serde_json::to_vec(payload).context("serialising frame")?;
    let len = u32::try_from(bytes.len()).context("payload length fits in u32")?;
    wr.write_all(&len.to_le_bytes()).await?;
    wr.write_all(&bytes).await?;
    wr.flush().await?;
    Ok(())
}
