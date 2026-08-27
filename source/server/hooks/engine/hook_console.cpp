
/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.

	LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
	DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
	DEALINGS IN LIFX.
*  =================================================================================== */

#include "hook_console.h"

__CM_INSTATNTIATE(_Engine_Con_InternalConsolePrintf);

// ---------------------------------------------------------------------------- //
void Hooks::Engine::OnInternalPrintf(U32 type, U32 unk1, char* buffer)
{
	std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	char t_buf[100] = { 0 };
	std::strftime(t_buf, sizeof(t_buf), "%H:%M:%S", std::localtime(&now));

	bool is_sql_report = false;
	std::string buff_str(buffer);
	std::string sql_prefix[] = { "DB::noRS", "DB::mNoRS", "DB::RS", "DB::mfRS", "DBI::", "DB_TRAN_ADD" };

	for (auto k : sql_prefix)
	{
		if (buff_str.compare(0, k.length(), k) == 0)
		{
			is_sql_report = true;
			break;
		}
	}

	if (gServer.GetConfig().UseExternalSQLLog && is_sql_report)
	{
		std::ofstream out(gServer.GetConfig().LogSQLFN, std::ios::app);
		if (out.is_open())
		{
			std::stringstream ss;
			ss << "[" << t_buf << "] : " << buffer << std::endl;
			out << ss.str();
			out.close();
		}
	}

	if (gServer.GetConfig().SkipConsoleSQLLogging && is_sql_report)
	{
		return;
	}

	if (gServer.GetConfig().UseExternalErrorLog && (type == 2))
	{
		std::ofstream out(gServer.GetConfig().LogErrorFN, std::ios::app);
		if (out.is_open())
		{
			std::stringstream ss;
			ss << "[" << t_buf << "] : " << buffer << std::endl;
			out << ss.str();
			out.close();
		}
	}

	// warning: cm_server has log types 4 (echo), 5 (warn), 6 (errr), 7 (info) (in lowercase)

	if (gServer.GetConfig().LogLevel > 0)
	{
		// skip info
		if (gServer.GetConfig().LogLevel >= 1 && (type == 3))
		{
			return;
		}

		// skip warnings & info
		if (gServer.GetConfig().LogLevel >= 2)
		{
			if (type == 1)
				return;
			if (type == 3)
				return;
		}

		// keep only errors
		if (gServer.GetConfig().LogLevel >= 3 && (type != 2))
		{
			return;
		}
	}

	char prefixed[8200];
	std::snprintf(prefixed, sizeof(prefixed), "[LiFx] %s", buffer);
	_Engine_Con_InternalConsolePrintf(type, unk1, prefixed);
}