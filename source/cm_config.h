#pragma once

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

// !!! DO NOT REMOVE OR CHANGE THIS VALUES !!!

#ifdef _YOUR_OWN_AURORA
static const bool kIsYourOwnDefaultServer = false;
#define _YOUR_OWN_VER 1500
static const int kYOVersion = _YOUR_OWN_VER;
static const char* kYOVersionString = "Life is Feudal Your Own 1.5.0.0";
#else
static const bool kIsYourOwnDefaultServer = true;
#define _YOUR_OWN_VER 1445
static const int kYOVersion = _YOUR_OWN_VER;
static const char* kYOVersionString = "Life is Feudal Your Own 1.4.4.5";
#endif

static const int kCoreVersion = 1;
static const char* kCoreVersionString = "LiFx ver. 1.0";
static const bool kCoreConstantValue = true;
