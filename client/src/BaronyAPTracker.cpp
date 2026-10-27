// Barony Archipelago tracker window.
//
// Barony (the Archipelago build) starts this with the game, and writes
// archipelago/tracker.txt about once a second. This window connects you (server, port and
// name go in archipelago/connect.txt, which the game reads), then shows that file:
// floor, what's left on the floor, lives, keys, deaths and the item log.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; } // gdiplus.h needs these without the macros
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace
{
	struct TrackerState
	{
		std::map<std::string, std::string> values;
		std::vector<std::string> items;
		std::vector<std::string> log;
		std::vector<std::pair<std::string, int>> deaths;
		bool fileFound = false;
	};

	TrackerState g_state;
	bool g_showAll = false;
	bool g_lootShort = false;   // the "other loot" box: full list (default) or just the total
	int g_lastLootToggle = -1;  // F4 presses counted by the game
	RECT g_lootBox{};           // where the loot box was drawn, for clicks
	const COLORREF STEEL = RGB(184, 196, 214);
	// The connect form: shown until the game says it's connected.
	HWND g_main = nullptr, g_editServer = nullptr, g_editPort = nullptr, g_editName = nullptr, g_btnConnect = nullptr;
	bool g_formShown = false;
	int g_formTop = -1;
	int g_sideX = 0;            // where the right-hand column starts (the map is left of it)
	std::wstring g_formNote;     // "enter a port", etc.
	HBRUSH g_editBrush = nullptr;
	const int ID_CONNECT = 101;
	const int ID_PLAIN = 102;
	HWND g_btnPlain = nullptr;
	bool g_launcher = false;          // opened by barony.exe before the game starts: choose how to play
	ULONGLONG g_gameStartedAt = 0;    // when Connect & Play let the game start
	HFONT g_titleFont = nullptr, g_bodyFont = nullptr;
	// Barony's own art, read from the Barony folder this program sits in.
	Gdiplus::Image* g_logo = nullptr;

	Gdiplus::Image* loadImage(const std::string& relative);

	const COLORREF BG = RGB(22, 18, 14);
	const COLORREF PANEL = RGB(38, 31, 24);
	const COLORREF GOLD = RGB(255, 200, 90);
	const COLORREF TEXT = RGB(225, 220, 210);
	const COLORREF DIM = RGB(150, 145, 135);
	const COLORREF GOOD = RGB(120, 230, 120);
	const COLORREF BAD = RGB(240, 90, 80);

	std::wstring widen(const std::string& s)
	{
		if ( s.empty() ) { return L""; }
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
		return w;
	}

	std::string exeDir()
	{
		char path[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		std::string p = path;
		size_t slash = p.find_last_of("\\/");
		return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
	}

	void readState()
	{
		TrackerState st;
		std::ifstream in(exeDir() + "\\archipelago\\tracker.txt");
		if ( in )
		{
			st.fileFound = true;
			std::string line;
			while ( std::getline(in, line) )
			{
				if ( !line.empty() && line.back() == '\r' ) { line.pop_back(); }
				size_t eq = line.find('=');
				if ( eq == std::string::npos ) { continue; }
				std::string k = line.substr(0, eq), v = line.substr(eq + 1);
				if ( k == "item" ) { st.items.push_back(v); st.log.push_back(v); }
				else if ( k == "log" ) { st.log.push_back(v); }
				else if ( k == "death" )
				{
					size_t tab = v.find('\t');
					if ( tab != std::string::npos ) { st.deaths.emplace_back(v.substr(0, tab), atoi(v.c_str() + tab + 1)); }
				}
				else { st.values[k] = v; }
			}
		}
		g_state = st;
		// F4 in the game flips the loot box between full and short.
		auto t = g_state.values.find("loot_toggle");
		if ( t != g_state.values.end() )
		{
			int v = atoi(t->second.c_str());
			if ( g_lastLootToggle >= 0 && v != g_lastLootToggle ) { g_lootShort = !g_lootShort; }
			g_lastLootToggle = v;
		}
	}

	Gdiplus::Image* loadImage(const std::string& relative)
	{
		std::string path = exeDir() + "\\" + relative;
		std::wstring wpath(path.begin(), path.end());
		Gdiplus::Image* img = Gdiplus::Image::FromFile(wpath.c_str());
		if ( img && img->GetLastStatus() != Gdiplus::Ok ) { delete img; img = nullptr; }
		return img;
	}


	std::string trim(const std::string& v)
	{
		size_t a = v.find_first_not_of(" \t\r\n"), b = v.find_last_not_of(" \t\r\n");
		return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
	}

	std::string getText(HWND edit)
	{
		wchar_t buf[256] = {};
		GetWindowTextW(edit, buf, 256);
		char out[512] = {};
		WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
		return trim(out);
	}

	std::map<std::string, std::string> readKeyValues(const std::string& path)
	{
		std::map<std::string, std::string> kv;
		std::ifstream in(path);
		std::string line;
		while ( in && std::getline(in, line) )
		{
			if ( !line.empty() && line.back() == '\r' ) { line.pop_back(); }
			size_t eq = line.find('=');
			if ( eq == std::string::npos || line[0] == '#' ) { continue; }
			kv[line.substr(0, eq)] = line.substr(eq + 1);
		}
		return kv;
	}

	// Last server, port and name used here; else archipelago.cfg; else archipelago.gg.
	void fillForm()
	{
		std::string host = "archipelago.gg", port, name;
		auto cfg = readKeyValues(exeDir() + "\\archipelago.cfg");
		if ( !cfg["server"].empty() )
		{
			std::string server = cfg["server"];
			size_t colon = server.rfind(':');
			if ( colon != std::string::npos ) { host = server.substr(0, colon); port = server.substr(colon + 1); }
			else { host = server; }
		}
		if ( !cfg["slot"].empty() ) { name = cfg["slot"]; }
		auto last = readKeyValues(exeDir() + "\\archipelago\\tracker_login.txt");
		if ( !last["host"].empty() ) { host = last["host"]; }
		if ( !last["port"].empty() ) { port = last["port"]; }
		if ( !last["slot"].empty() ) { name = last["slot"]; }
		SetWindowTextW(g_editServer, widen(host).c_str());
		SetWindowTextW(g_editPort, widen(port).c_str());
		SetWindowTextW(g_editName, widen(name).c_str());
	}

	bool gameRunning()
	{
		HANDLE m = OpenMutexA(SYNCHRONIZE, FALSE, "BaronyAPGameRunning");
		if ( !m ) { return false; }
		CloseHandle(m);
		return true;
	}

	void doConnect()
	{
		std::string host = getText(g_editServer), port = getText(g_editPort), name = getText(g_editName);
		if ( host.empty() ) { host = "archipelago.gg"; }
		if ( host.find(':') != std::string::npos && port.empty() )
		{
			port = host.substr(host.rfind(':') + 1);
			host = host.substr(0, host.rfind(':'));
		}
		if ( port.empty() || port.find_first_not_of("0123456789") != std::string::npos )
		{
			g_formNote = L"Type the room's port number (the digits after the colon).";
		}
		else if ( name.empty() )
		{
			g_formNote = L"Type your name (your slot name from your YAML).";
		}
		else if ( !g_launcher && !gameRunning() )
		{
			g_formNote = L"Start Barony first, then press Connect.";
		}
		else
		{
			std::string dir = exeDir() + "\\archipelago";
			CreateDirectoryA(dir.c_str(), nullptr);
			{
				std::ofstream login(dir + "\\tracker_login.txt", std::ios::trunc);
				login << "host=" << host << "\nport=" << port << "\nslot=" << name << "\n";
			}
			std::ofstream out(dir + "\\connect.txt", std::ios::trunc);
			out << "server=" << host << ":" << port << "\nslot=" << name << "\n";
			out.close();
			g_formNote = L"Connecting...";
			if ( g_launcher )
			{
				// Let the waiting barony.exe start the game; this window becomes the tracker.
				HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, "BaronyAPLaunchConnect");
				if ( ev ) { SetEvent(ev); CloseHandle(ev); }
				g_launcher = false;
				g_gameStartedAt = GetTickCount64();
				ShowWindow(g_btnPlain, SW_HIDE);
				InvalidateRect(g_btnConnect, nullptr, TRUE);
			}
		}
		InvalidateRect(g_main, nullptr, FALSE);
	}

	void playPlain()
	{
		HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, "BaronyAPLaunchPlain");
		if ( ev ) { SetEvent(ev); CloseHandle(ev); }
		DestroyWindow(g_main);
	}

	void showForm(bool show)
	{
		if ( show == g_formShown ) { return; }
		g_formShown = show;
		int cmd = show ? SW_SHOW : SW_HIDE;
		ShowWindow(g_editServer, cmd);
		ShowWindow(g_editPort, cmd);
		ShowWindow(g_editName, cmd);
		ShowWindow(g_btnConnect, cmd);
		ShowWindow(g_btnPlain, show && g_launcher ? SW_SHOW : SW_HIDE);
		if ( show ) { SetFocus(GetWindowTextLengthW(g_editPort) ? g_editName : g_editPort); }
		else { SetFocus(g_main); g_formNote.clear(); }
	}

	void placeForm(int top, int width)
	{
		if ( top == g_formTop ) { return; }
		g_formTop = top;
		int x = 150, w = width - x - 20, ox = g_sideX;
		SetWindowPos(g_editServer, nullptr, ox + x, top, w, 34, SWP_NOZORDER);
		SetWindowPos(g_editPort, nullptr, ox + x, top + 44, 140, 34, SWP_NOZORDER);
		SetWindowPos(g_editName, nullptr, ox + x, top + 88, w, 34, SWP_NOZORDER);
		SetWindowPos(g_btnConnect, nullptr, ox + x, top + 136, 210, 42, SWP_NOZORDER);
		SetWindowPos(g_btnPlain, nullptr, ox + 20, top + 196, width - 40, 42, SWP_NOZORDER);
	}

	void drawPanel(HDC dc, int x, int y, int w, int h)
	{
		Gdiplus::Graphics g(dc);
		Gdiplus::SolidBrush b(Gdiplus::Color(200, 30, 24, 18));
		g.FillRectangle(&b, x, y, w, h);
		Gdiplus::Pen pen(Gdiplus::Color(255, 74, 62, 48), 1.0f);
		g.DrawRectangle(&pen, x, y, w - 1, h - 1);
	}

	int drawLogo(HDC dc, int x, int y)
	{
		if ( !g_logo ) { return 0; }
		Gdiplus::Graphics g(dc);
		g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		int w = (int)g_logo->GetWidth() * 2, h = (int)g_logo->GetHeight() * 2;
		g.DrawImage(g_logo, x, y, w, h);
		return h;
	}

	int num(const char* key)
	{
		auto it = g_state.values.find(key);
		return it == g_state.values.end() ? 0 : atoi(it->second.c_str());
	}

	void text(HDC dc, int x, int y, const std::wstring& s, COLORREF c, HFONT f)
	{
		SelectObject(dc, f);
		SetTextColor(dc, c);
		TextOutW(dc, x, y, s.c_str(), (int)s.size());
	}

	void fill(HDC dc, RECT r, COLORREF c)
	{
		HBRUSH b = CreateSolidBrush(c);
		FillRect(dc, &r, b);
		DeleteObject(b);
	}


	// ---------------------------------------------------------------- the floor map
	// The game writes archipelago/trackermap.txt: only what Barony's own map knows (the layout
	// you've seen, items where you've seen them), plus Archipelago items and green enemies.
	const COLORREF SECRET = RGB(190, 140, 255);
	struct MapItem { double x, y; std::string path, name; bool gold = false; };
	struct MapPile { int tx, ty; std::vector<MapItem> items; };
	struct MapPoint { double x, y; std::string kind; };
	struct MapState
	{
		bool ingame = false;
		int w = 0, h = 0;
		std::string floorId, name;
		std::vector<std::string> rows;
		bool hasPlayer = false;
		double px = 0, py = 0, pyaw = 0;
		std::vector<MapPoint> ap, foes, chests, stairs, secrets;
		bool hasKey = false;
		double kx = 0, ky = 0;
		bool locked = false;
		std::vector<MapPile> piles;
	};
	MapState g_map;
	int g_mapX = 0, g_mapY = 0, g_mapCell = 0;
	int g_hoverX = -1, g_hoverY = -1, g_mouseX = 0, g_mouseY = 0;
	std::map<std::string, Gdiplus::Image*> g_icons;

	std::vector<std::string> splitTabs(const std::string& v)
	{
		std::vector<std::string> out;
		size_t start = 0;
		while ( true )
		{
			size_t tab = v.find('\t', start);
			out.push_back(v.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
			if ( tab == std::string::npos ) { break; }
			start = tab + 1;
		}
		return out;
	}

	void readMap()
	{
		MapState m;
		std::ifstream in(exeDir() + "\\archipelago\\trackermap.txt");
		std::string line;
		std::map<long long, size_t> pileIndex;
		while ( in && std::getline(in, line) )
		{
			if ( !line.empty() && line.back() == '\r' ) { line.pop_back(); }
			size_t eq = line.find('=');
			if ( eq == std::string::npos ) { continue; }
			std::string k = line.substr(0, eq), v = line.substr(eq + 1);
			auto f = splitTabs(v);
			auto d = [&](size_t i) { return i < f.size() ? atof(f[i].c_str()) : 0.0; };
			if ( k == "ingame" ) { m.ingame = v == "1"; }
			else if ( k == "size" ) { sscanf(v.c_str(), "%d %d", &m.w, &m.h); }
			else if ( k == "floorid" ) { m.floorId = v; }
			else if ( k == "name" ) { m.name = v; }
			else if ( k == "row" ) { m.rows.push_back(v); }
			else if ( k == "player" ) { m.hasPlayer = true; m.px = d(0); m.py = d(1); m.pyaw = d(2); }
			else if ( k == "ap" ) { m.ap.push_back({ d(0), d(1), f.size() > 2 ? f[2] : "" }); }
			else if ( k == "foe" ) { m.foes.push_back({ d(0), d(1), "" }); }
			else if ( k == "chest" ) { m.chests.push_back({ d(0), d(1), "" }); }
			else if ( k == "stairs" ) { m.stairs.push_back({ d(0), d(1), "" }); }
			else if ( k == "secret" ) { m.secrets.push_back({ d(0), d(1), "" }); }
			else if ( k == "key" ) { m.hasKey = true; m.kx = d(0); m.ky = d(1); }
			else if ( k == "locked" ) { m.locked = v == "1"; }
			else if ( k == "item" || k == "gold" )
			{
				MapItem it;
				it.x = d(0); it.y = d(1);
				if ( k == "gold" ) { it.gold = true; it.name = (f.size() > 2 ? f[2] : std::string("Some")) + " gold"; }
				else { it.path = f.size() > 2 ? f[2] : ""; it.name = f.size() > 3 ? f[3] : "an item"; }
				int tx = (int)it.x, ty = (int)it.y;
				long long key = (long long)ty * 100000 + tx;
				auto found = pileIndex.find(key);
				if ( found == pileIndex.end() )
				{
					pileIndex[key] = m.piles.size();
					m.piles.push_back(MapPile{ tx, ty, { it } });
				}
				else { m.piles[found->second].items.push_back(it); }
			}
		}
		if ( (int)m.rows.size() != m.h ) { m.ingame = false; }
		g_map = m;
	}

	const MapPile* pileAt(int tx, int ty)
	{
		if ( tx < 0 || ty < 0 ) { return nullptr; }
		for ( auto& p : g_map.piles ) { if ( p.tx == tx && p.ty == ty ) { return &p; } }
		return nullptr;
	}

	Gdiplus::Image* itemIcon(const std::string& gamePath)
	{
		std::string rel = gamePath;
		while ( !rel.empty() && (rel[0] == '*' || rel[0] == '#') ) { rel.erase(0, 1); }
		if ( rel.empty() ) { return nullptr; }
		auto it = g_icons.find(rel);
		if ( it != g_icons.end() ) { return it->second; }
		std::string win = rel;
		std::replace(win.begin(), win.end(), '/', '\\');
		Gdiplus::Image* img = loadImage(win);
		g_icons[rel] = img; // nullptr too, so a missing file isn't retried every second
		return img;
	}

	void drawIcon(Gdiplus::Graphics& g, const MapItem& it, float cx, float cy, float size)
	{
		Gdiplus::SolidBrush back(Gdiplus::Color(215, 22, 18, 14));
		Gdiplus::Pen edge(Gdiplus::Color(255, 0, 0, 0), 1.5f);
		g.FillRectangle(&back, cx - size / 2, cy - size / 2, size, size);
		g.DrawRectangle(&edge, cx - size / 2, cy - size / 2, size, size);
		Gdiplus::Image* img = it.gold ? nullptr : itemIcon(it.path);
		if ( img )
		{
			g.DrawImage(img, cx - size / 2 + 1, cy - size / 2 + 1, size - 2, size - 2);
		}
		else if ( it.gold )
		{
			Gdiplus::SolidBrush coin(Gdiplus::Color(255, 255, 200, 90));
			g.FillEllipse(&coin, cx - size / 4, cy - size / 4, size / 2, size / 2);
			g.DrawEllipse(&edge, cx - size / 4, cy - size / 4, size / 2, size / 2);
		}
		else
		{
			Gdiplus::SolidBrush dot(Gdiplus::Color(255, 225, 220, 210));
			g.FillRectangle(&dot, cx - 3, cy - 3, 6.0f, 6.0f);
		}
	}

	void paintMap(HDC dc, RECT area)
	{
		drawPanel(dc, area.left, area.top, area.right - area.left, area.bottom - area.top);
		g_mapCell = 0;
		int top = area.top + 10;
		if ( !g_map.ingame || g_map.w <= 0 || g_map.h <= 0 )
		{
			text(dc, area.left + 16, top, L"Floor map", GOLD, g_bodyFont);
			text(dc, area.left + 16, top + 34, L"Start a run to see your floor here.", DIM, g_bodyFont);
			return;
		}
		wchar_t buf[256];
		swprintf(buf, 256, L"Floor %d: %s", num("floor"), widen(g_map.name).c_str());
		text(dc, area.left + 16, top, buf, GOLD, g_bodyFont);
		top += 36;
		int legendH = 30;
		int availW = area.right - area.left - 24, availH = area.bottom - top - legendH - 12;
		int cell = std::max(2, std::min(availW / g_map.w, availH / g_map.h));
		int mw = cell * g_map.w, mh = cell * g_map.h;
		int mx = area.left + 12 + (availW - mw) / 2, my = top + std::max(0, (availH - mh) / 2);
		g_mapX = mx; g_mapY = my; g_mapCell = cell;

		// The layout: one pixel per tile, stretched (Barony's own map colors).
		BITMAPINFO bi{};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = g_map.w;
		bi.bmiHeader.biHeight = -g_map.h;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		std::vector<uint32_t> px((size_t)g_map.w * g_map.h);
		auto rgb = [](int r, int g2, int b) { return (uint32_t)((r << 16) | (g2 << 8) | b); };
		for ( int y = 0; y < g_map.h; ++y )
		{
			const std::string& row = g_map.rows[y];
			for ( int x = 0; x < g_map.w; ++x )
			{
				char c = x < (int)row.size() ? row[x] : '.';
				uint32_t col = rgb(0, 40, 40);
				if ( c == 'f' ) { col = rgb(0, 128, 128); }
				else if ( c == 'w' ) { col = rgb(0, 255, 255); }
				else if ( c == 'g' ) { col = rgb(64, 64, 64); }
				else if ( c == 'h' ) { col = rgb(128, 128, 128); }
				px[(size_t)y * g_map.w + x] = col;
			}
		}
		SetStretchBltMode(dc, COLORONCOLOR);
		StretchDIBits(dc, mx, my, mw, mh, 0, 0, g_map.w, g_map.h, px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);

		Gdiplus::Graphics g(dc);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		float icon = (float)std::min(28, std::max(14, cell * 3 / 2));
		auto sx = [&](double tx) { return (float)(mx + tx * cell); };
		auto sy = [&](double ty) { return (float)(my + ty * cell); };
		auto tileCenterX = [&](int tx) { return (float)(mx + tx * cell + cell / 2.0); };
		auto tileCenterY = [&](int ty) { return (float)(my + ty * cell + cell / 2.0); };
		Gdiplus::Pen black(Gdiplus::Color(255, 0, 0, 0), 2.0f);

		for ( auto& c : g_map.chests )
		{
			Gdiplus::SolidBrush wood(Gdiplus::Color(255, 138, 90, 43));
			float w = icon * 0.8f, h = icon * 0.6f;
			g.FillRectangle(&wood, sx(c.x) - w / 2, sy(c.y) - h / 2, w, h);
			g.DrawRectangle(&black, sx(c.x) - w / 2, sy(c.y) - h / 2, w, h);
		}
		for ( auto& st : g_map.stairs )
		{
			Gdiplus::Color c = g_map.locked ? Gdiplus::Color(255, 240, 90, 80) : Gdiplus::Color(255, 120, 230, 120);
			Gdiplus::Pen pen(c, 2.0f);
			Gdiplus::SolidBrush back(Gdiplus::Color(255, 22, 18, 14));
			float s2 = icon * 0.9f;
			g.FillRectangle(&back, sx(st.x) - s2 / 2, sy(st.y) - s2 / 2, s2, s2);
			g.DrawRectangle(&pen, sx(st.x) - s2 / 2, sy(st.y) - s2 / 2, s2, s2);
			for ( int i = -1; i <= 1; ++i ) { g.DrawLine(&pen, sx(st.x) - s2 / 3, sy(st.y) + i * s2 / 4, sx(st.x) + s2 / 3, sy(st.y) + i * s2 / 4); }
		}
		for ( auto& st : g_map.secrets )
		{
			Gdiplus::Pen pen(Gdiplus::Color(255, 190, 140, 255), 2.0f);
			Gdiplus::SolidBrush back(Gdiplus::Color(255, 40, 20, 60));
			float s2 = icon * 0.9f;
			g.FillEllipse(&back, sx(st.x) - s2 / 2, sy(st.y) - s2 / 2, s2, s2);
			g.DrawEllipse(&pen, sx(st.x) - s2 / 2, sy(st.y) - s2 / 2, s2, s2);
			g.DrawLine(&pen, sx(st.x), sy(st.y) - s2 / 4, sx(st.x), sy(st.y) + s2 / 4);
		}
		for ( auto& p : g_map.piles )
		{
			float cx = tileCenterX(p.tx), cy = tileCenterY(p.ty);
			drawIcon(g, p.items.front(), cx, cy, icon);
			if ( p.items.size() > 1 )
			{
				Gdiplus::SolidBrush badge(Gdiplus::Color(255, 255, 200, 90));
				Gdiplus::Pen bpen(Gdiplus::Color(255, 0, 0, 0), 1.5f);
				float bw = 13, bh = 12, bx = cx + icon / 2 - 6, by = cy - icon / 2 - 7;
				g.FillRectangle(&badge, bx, by, bw, bh);
				g.DrawRectangle(&bpen, bx, by, bw, bh);
				Gdiplus::FontFamily ff(L"Consolas");
				Gdiplus::Font font(&ff, 9.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
				Gdiplus::SolidBrush ink(Gdiplus::Color(255, 0, 0, 0));
				Gdiplus::StringFormat sf;
				sf.SetAlignment(Gdiplus::StringAlignmentCenter);
				sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
				std::wstring n = std::to_wstring(std::min<size_t>(p.items.size(), 99));
				g.DrawString(n.c_str(), -1, &font, Gdiplus::RectF(bx, by, bw, bh), &sf, &ink);
			}
		}
		for ( auto& a : g_map.ap )
		{
			float r = icon * 0.42f, cx = sx(a.x), cy = sy(a.y);
			if ( a.kind == "chest" )
			{
				Gdiplus::SolidBrush wood(Gdiplus::Color(255, 138, 90, 43));
				g.FillRectangle(&wood, cx - icon * 0.4f, cy - icon * 0.3f, icon * 0.8f, icon * 0.6f);
				g.DrawRectangle(&black, cx - icon * 0.4f, cy - icon * 0.3f, icon * 0.8f, icon * 0.6f);
				cy -= icon * 0.45f;
				r = icon * 0.25f;
			}
			Gdiplus::SolidBrush ring(Gdiplus::Color(255, 22, 18, 14));
			Gdiplus::SolidBrush green(Gdiplus::Color(255, 120, 230, 120));
			g.FillEllipse(&ring, cx - r - 2, cy - r - 2, 2 * r + 4, 2 * r + 4);
			g.DrawEllipse(&black, cx - r - 2, cy - r - 2, 2 * r + 4, 2 * r + 4);
			g.FillEllipse(&green, cx - r, cy - r, 2 * r, 2 * r);
			g.FillEllipse(&ring, cx - r * 0.4f, cy - r * 0.4f, r * 0.8f, r * 0.8f);
		}
		for ( auto& f : g_map.foes )
		{
			float r = icon * 0.4f, cx = sx(f.x), cy = sy(f.y);
			Gdiplus::PointF pts[4] = { { cx, cy - r }, { cx + r, cy }, { cx, cy + r }, { cx - r, cy } };
			Gdiplus::SolidBrush body(Gdiplus::Color(255, 47, 138, 47));
			g.FillPolygon(&body, pts, 4);
			g.DrawPolygon(&black, pts, 4);
		}
		if ( g_map.hasKey )
		{
			float cx = sx(g_map.kx), cy = sy(g_map.ky);
			if ( g_map.hasPlayer )
			{
				Gdiplus::Pen dash(Gdiplus::Color(200, 255, 200, 90), 2.0f);
				dash.SetDashStyle(Gdiplus::DashStyleDash);
				g.DrawLine(&dash, sx(g_map.px), sy(g_map.py), cx, cy);
			}
			Gdiplus::Pen outline(Gdiplus::Color(255, 0, 0, 0), 5.0f), gold(Gdiplus::Color(255, 255, 200, 90), 2.5f);
			float r = icon * 0.22f;
			for ( Gdiplus::Pen* pen : { &outline, &gold } )
			{
				g.DrawEllipse(pen, cx - r * 2, cy - r, 2 * r, 2 * r);
				g.DrawLine(pen, cx, cy, cx + r * 2.2f, cy);
				g.DrawLine(pen, cx + r * 1.6f, cy, cx + r * 1.6f, cy + r);
				g.DrawLine(pen, cx + r * 2.2f, cy, cx + r * 2.2f, cy + r);
			}
		}
		if ( g_map.hasPlayer )
		{
			float cx = sx(g_map.px), cy = sy(g_map.py), r = icon * 0.5f;
			double a = g_map.pyaw; // 0 = east, +y = south
			auto pt = [&](double ang, float len) { return Gdiplus::PointF(cx + (float)(cos(a + ang) * len), cy + (float)(sin(a + ang) * len)); };
			Gdiplus::PointF tri[4] = { pt(0, r), pt(2.5, r * 0.85f), pt(3.14159, r * 0.35f), pt(-2.5, r * 0.85f) };
			Gdiplus::SolidBrush you(Gdiplus::Color(255, 255, 216, 74));
			g.FillPolygon(&you, tri, 4);
			g.DrawPolygon(&black, tri, 4);
		}

		g.Flush(Gdiplus::FlushIntentionSync);
		// Legend under the map.
		int ly = area.bottom - legendH - 4;
		text(dc, area.left + 16, ly, L"Green: Archipelago   Diamond: green enemy   Hover a pile to see it", DIM, g_bodyFont);

		// Hovering a pile: what's in it.
		const MapPile* hp = pileAt(g_hoverX, g_hoverY);
		if ( hp )
		{
			int rowH = 26, wBox = 320, hBox = 36 + rowH * (int)std::min<size_t>(hp->items.size(), 12);
			int bx = std::min(g_mouseX + 16, (int)area.right - wBox - 4), by = std::min(g_mouseY + 12, (int)area.bottom - hBox - 4);
			Gdiplus::SolidBrush back(Gdiplus::Color(242, 16, 12, 9));
			Gdiplus::Pen border(Gdiplus::Color(255, 255, 200, 90), 2.0f);
			g.FillRectangle(&back, bx, by, wBox, hBox);
			g.DrawRectangle(&border, bx, by, wBox, hBox);
			g.Flush(Gdiplus::FlushIntentionSync);
			swprintf(buf, 256, hp->items.size() == 1 ? L"1 item here" : L"%d items here", (int)hp->items.size());
			text(dc, bx + 10, by + 4, buf, GOLD, g_bodyFont);
			Gdiplus::Graphics g2(dc);
			g2.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
			g2.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
			for ( size_t i = 0; i < hp->items.size() && i < 12; ++i )
			{
				int ry = by + 34 + (int)i * rowH;
				drawIcon(g2, hp->items[i], (float)bx + 20, (float)ry + 10, 20);
				std::wstring nm = widen(hp->items[i].name);
				if ( !nm.empty() ) { nm[0] = towupper(nm[0]); }
				text(dc, bx + 38, ry - 4, nm, TEXT, g_bodyFont);
			}
		}
	}

	// The right-hand column (or the whole window when it's too narrow for the map).
	void paintSidebar(HDC dc, RECT rc)
	{
		SetBkMode(dc, TRANSPARENT);
		fill(dc, rc, BG);

		int y = 8;
		int logoH = g_logo ? drawLogo(dc, (rc.right - (int)g_logo->GetWidth() * 2) / 2, y) : 0;
		if ( logoH > 0 )
		{
			y += logoH + 6;
		}
		else
		{
			text(dc, 14, y, L"Barony Archipelago", GOLD, g_titleFont);
			y += 34;
		}

		bool running = gameRunning();
		bool connected = running && g_state.fileFound && num("connected") != 0;
		showForm(!connected);
		if ( !connected )
		{
			std::string status = running ? g_state.values["status"] : std::string("off");
			if ( !running && g_gameStartedAt && GetTickCount64() - g_gameStartedAt < 90000 ) { status = "starting"; }
			if ( g_launcher ) { status = "launcher"; }
			std::wstring line;
			COLORREF c = TEXT;
			if ( status == "launcher" ) { line = L"Connect to your Archipelago room, or play without it:"; }
			else if ( status == "starting" ) { line = L"Starting Barony..."; c = GOLD; }
			else if ( status == "off" ) { line = L"Start Barony, then connect here."; c = DIM; }
			else if ( status == "connecting" )
			{
				line = L"Connecting to " + widen(g_state.values["server"]) + L" as " + widen(g_state.values["slot"]) + L"...";
				c = GOLD;
			}
			else if ( status == "refused" ) { line = L"The server refused that name. Check it and connect again."; c = BAD; }
			else if ( status == "lost" ) { line = L"Lost the connection. Check the port and connect again."; c = BAD; }
			else { line = L"Connect to your Archipelago room:"; }
			RECT tr{ 14, y, rc.right - 14, y + 60 };
			SelectObject(dc, g_bodyFont);
			SetTextColor(dc, c);
			int th = DrawTextW(dc, line.c_str(), -1, &tr, DT_WORDBREAK);
			y += th + 12;
			text(dc, 20, y + 2, L"Server", TEXT, g_bodyFont);
			text(dc, 20, y + 46, L"Port", TEXT, g_bodyFont);
			text(dc, 20, y + 90, L"Name", TEXT, g_bodyFont);
			placeForm(y, rc.right);
			y += g_launcher ? 250 : 190;
			if ( !g_formNote.empty() && g_formNote != L"Connecting..." && status != "connecting" )
			{
				RECT nr{ 14, y, rc.right - 14, y + 60 };
				SetTextColor(dc, g_formNote == L"Connecting..." ? GOLD : BAD);
				y += DrawTextW(dc, g_formNote.c_str(), -1, &nr, DT_WORDBREAK) + 8;
			}
			text(dc, 14, y, L"No password needed. Press Enter or Connect.", DIM, g_bodyFont);
		}
		else
		{
			std::wstring slot = widen(g_state.values["slot"]);
			text(dc, 14, y, L"Connected as " + slot, GOOD, g_bodyFont);
			y += 26;

			if ( num("ingame") )
			{
				wchar_t buf[256];
				swprintf(buf, 256, L"Floor %d     Lives %d", num("floor"), num("lives"));
				text(dc, 14, y, buf, TEXT, g_bodyFont);
				y += 24;
				int key = num("floorkey");
				if ( key == 0 && num("keyreq") == 2 )
				{
					swprintf(buf, 256, L"Floor Key: defeat all enemies (%d left)", std::max(0, num("enemiesleft")));
					text(dc, 14, y, buf, GOLD, g_bodyFont);
					y += 24;
				}
				else if ( key == 0 )
				{
					std::wstring compass = widen(g_state.values["keycompass"]);
					text(dc, 14, y, compass.empty() ? L"Floor Key: not found yet" : L"Floor Key: " + compass, GOLD, g_bodyFont);
					y += 24;
					int left = num("enemiesleft");
					if ( left > 0 && num("keyreq") == 0 )
					{
						swprintf(buf, 256, L"  or defeat all enemies (%d left)", left);
						text(dc, 14, y, buf, DIM, g_bodyFont);
						y += 22;
					}
				}
				else if ( key == 1 ) { text(dc, 14, y, L"Floor Key: found, the stairs are open", GOOD, g_bodyFont); y += 24; }
				int mino = num("minotaur");
				if ( mino >= 0 && g_state.values.count("minotaur") )
				{
					swprintf(buf, 256, L"Minotaur arrives in %d:%02d", mino / 60, mino % 60);
					text(dc, 14, y, buf, mino <= 30 ? BAD : RGB(240, 150, 110), g_bodyFont);
					y += 24;
				}
				else if ( mino == -2 )
				{
					text(dc, 14, y, L"The Minotaur is hunting you!", BAD, g_bodyFont);
					y += 24;
				}
				if ( num("secretarea") )
				{
					text(dc, 14, y, L"You're in a secret area", SECRET, g_bodyFont);
					y += 24;
				}
				else if ( num("secretexit") )
				{
					text(dc, 14, y, g_map.secrets.empty() ? L"Secret area entrance on this floor" : L"Secret area entrance: found", SECRET, g_bodyFont);
					y += 24;
				}
				y += 6;

				drawPanel(dc, 10, y, rc.right - 20, 118);
				int ground = num("ground"), chests = num("chests"), shop = num("shop"), marked = num("marked");
				int total = ground + chests + shop + marked;
				text(dc, 20, y + 8, L"Archipelago on this floor", GOLD, g_bodyFont);
				swprintf(buf, 256, L"%d", total);
				text(dc, rc.right - 60, y + 8, buf, total ? GOOD : DIM, g_titleFont);
				swprintf(buf, 256, L"On the ground: %d", ground);
				text(dc, 30, y + 36, buf, ground ? GOOD : DIM, g_bodyFont);
				swprintf(buf, 256, L"In chests: %d", chests);
				text(dc, 30, y + 56, buf, chests ? GOOD : DIM, g_bodyFont);
				swprintf(buf, 256, L"In the shop: %d", shop);
				text(dc, 30, y + 76, buf, shop ? GOOD : DIM, g_bodyFont);
				swprintf(buf, 256, L"Green enemies: %d", marked);
				text(dc, 30, y + 96, buf, marked ? GOOD : DIM, g_bodyFont);
				y += 128;
				swprintf(buf, 256, L"Archipelago pickups still to find: %d", num("pickups_left"));
				text(dc, 14, y, buf, DIM, g_bodyFont);
				y += 22;
				// Enemy Sanity progress: green kills toward the next check, or checks left.
				if ( num("es_mode") == 1 && num("es_left") > 0 )
				{
					swprintf(buf, 256, L"Enemy Sanity: %d / %d green kills  (%d checks left)", num("es_progress"), num("es_per"), num("es_left"));
					text(dc, 14, y, buf, GOOD, g_bodyFont);
					y += 22;
				}
				else if ( num("es_mode") == 2 && num("es_left") > 0 )
				{
					swprintf(buf, 256, L"Enemy Sanity: %d green enemies left to kill", num("es_left"));
					text(dc, 14, y, buf, GOOD, g_bodyFont);
					y += 22;
				}
				y += 8;

				// Other (ordinary) loot lying on the floor: full list, or just the total (F4 / L / click).
				struct Row { const wchar_t* name; int n; };
				Row rows[] = {
					{ L"Weapons", num("loot_weapon") }, { L"Armor", num("loot_armor") },
					{ L"Potions", num("loot_potion") }, { L"Scrolls", num("loot_scroll") },
					{ L"Spellbooks", num("loot_spellbook") }, { L"Magic staffs", num("loot_staff") },
					{ L"Rings, amulets", num("loot_ring") + num("loot_amulet") }, { L"Gems", num("loot_gem") },
					{ L"Tools", num("loot_tool") }, { L"Food", num("loot_food") },
					{ L"Thrown", num("loot_thrown") }, { L"Books", num("loot_book") },
				};
				int boxH = g_lootShort ? 44 : 44 + 6 * 21 + 14 + 22 + 22;
				drawPanel(dc, 10, y, rc.right - 20, boxH);
				g_lootBox = RECT{ 10, y, rc.right - 10, y + boxH };
				text(dc, 20, y + 10, g_lootShort ? L"Normal loot on this floor  (F4: more)" : L"Normal loot on this floor", STEEL, g_bodyFont);
				swprintf(buf, 256, L"%d", num("loot_total"));
				text(dc, rc.right - 60, y + 6, buf, num("loot_total") ? GOOD : DIM, g_titleFont);
				if ( !g_lootShort )
				{
					int colW = (rc.right - 60) / 2;
					for ( int i = 0; i < 12; ++i )
					{
						int cx = 30 + (i % 2) * (colW + 10), cy = y + 40 + (i / 2) * 21;
						COLORREF c = rows[i].n ? GOOD : DIM;
						text(dc, cx, cy, rows[i].name, c, g_bodyFont);
						swprintf(buf, 256, L"%d", rows[i].n);
						text(dc, cx + colW - 30, cy, buf, c, g_bodyFont);
					}
					int ly = y + 40 + 6 * 21 + 6;
					swprintf(buf, 256, L"Gold piles  %d", num("loot_gold"));
					text(dc, 30, ly, buf, num("loot_gold") ? GOOD : DIM, g_bodyFont);
					swprintf(buf, 256, L"Chests with loot  %d", num("loot_chests"));
					text(dc, 30 + colW + 10, ly, buf, num("loot_chests") ? GOOD : DIM, g_bodyFont);
					text(dc, 30, ly + 22, L"On the floor only; chest contents aren't counted.", DIM, g_bodyFont);
				}
				y += boxH + 12;
			}
			else
			{
				text(dc, 14, y, L"Start a run to see your floor.", DIM, g_bodyFont);
				y += 30;
			}

			{
				int rowsH = 34 + 20 * (int)std::max<size_t>(1, g_state.deaths.size());
				drawPanel(dc, 10, y - 4, rc.right - 20, rowsH);
				wchar_t buf[256];
				swprintf(buf, 256, L"Deaths: %d", num("deaths_total"));
				text(dc, 14, y, buf, GOLD, g_bodyFont);
				y += 22;
				auto rows = g_state.deaths;
				std::stable_sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.second > b.second; });
				for ( auto& r : rows )
				{
					bool me = r.first == g_state.values["slot"];
					text(dc, 30, y, widen(r.first), me ? GOLD : TEXT, g_bodyFont);
					swprintf(buf, 256, L"%d", r.second);
					text(dc, rc.right - 60, y, buf, me ? GOLD : TEXT, g_bodyFont);
					y += 20;
				}
				if ( rows.empty() ) { text(dc, 30, y, L"Nobody has died yet.", DIM, g_bodyFont); y += 20; }
				y += 14;
			}

			text(dc, 14, y, g_showAll ? L"Full log   (Tab: items only)" : L"Item log   (Tab: full log)", GOLD, g_bodyFont);
			y += 24;
			const auto& lines = g_showAll ? g_state.log : g_state.items;
			int room = (rc.bottom - y - 8) / 20;
			int start = (int)lines.size() > room ? (int)lines.size() - room : 0;
			if ( lines.empty() ) { text(dc, 14, y, L"Nothing yet.", DIM, g_bodyFont); }
			for ( int i = start; i < (int)lines.size(); ++i, y += 20 )
			{
				text(dc, 14, y, widen(lines[i]), TEXT, g_bodyFont);
			}
		}

	}

	void paint(HWND hwnd)
	{
		PAINTSTRUCT ps;
		HDC wdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		HDC dc = CreateCompatibleDC(wdc);
		HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
		HGDIOBJ oldBmp = SelectObject(dc, bmp);
		SetBkMode(dc, TRANSPARENT);
		fill(dc, rc, BG);

		// Wide window: the floor map on the left, everything else in a column on the right.
		bool withMap = rc.right >= 900;
		int sideW = withMap ? 450 : rc.right;
		int sideX = rc.right - sideW;
		if ( sideX != g_sideX ) { g_sideX = sideX; g_formTop = -1; }
		{
			HDC sdc = CreateCompatibleDC(wdc);
			HBITMAP sbmp = CreateCompatibleBitmap(wdc, sideW, rc.bottom);
			HGDIOBJ sold = SelectObject(sdc, sbmp);
			paintSidebar(sdc, RECT{ 0, 0, sideW, rc.bottom });
			BitBlt(dc, sideX, 0, sideW, rc.bottom, sdc, 0, 0, SRCCOPY);
			SelectObject(sdc, sold);
			DeleteObject(sbmp);
			DeleteDC(sdc);
		}
		if ( withMap ) { paintMap(dc, RECT{ 10, 10, sideX - 6, rc.bottom - 10 }); }
		else { g_mapCell = 0; }

		BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
		SelectObject(dc, oldBmp);
		DeleteObject(bmp);
		DeleteDC(dc);
		EndPaint(hwnd, &ps);
	}

	LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		switch ( msg )
		{
			case WM_TIMER:
				readState();
				readMap();
				InvalidateRect(hwnd, nullptr, FALSE);
				return 0;
			case WM_KEYDOWN:
				if ( wp == VK_TAB ) { g_showAll = !g_showAll; InvalidateRect(hwnd, nullptr, FALSE); }
				if ( wp == 'L' || wp == VK_F4 ) { g_lootShort = !g_lootShort; InvalidateRect(hwnd, nullptr, FALSE); }
				return 0;
			case WM_COMMAND:
				if ( LOWORD(wp) == ID_CONNECT ) { doConnect(); }
				if ( LOWORD(wp) == ID_PLAIN ) { playPlain(); }
				return 0;
			case WM_CTLCOLOREDIT:
				SetTextColor((HDC)wp, TEXT);
				SetBkColor((HDC)wp, PANEL);
				return (LRESULT)g_editBrush;
			case WM_DRAWITEM:
			{
				auto* di = (DRAWITEMSTRUCT*)lp;
				if ( di->CtlID != ID_CONNECT && di->CtlID != ID_PLAIN ) { break; }
				bool plain = di->CtlID == ID_PLAIN;
				bool down = (di->itemState & ODS_SELECTED) != 0;
				fill(di->hDC, di->rcItem, down ? RGB(90, 70, 30) : RGB(60, 46, 26));
				HBRUSH border = CreateSolidBrush(plain ? DIM : GOLD);
				FrameRect(di->hDC, &di->rcItem, border);
				DeleteObject(border);
				SetBkMode(di->hDC, TRANSPARENT);
				SelectObject(di->hDC, g_bodyFont);
				SetTextColor(di->hDC, plain ? TEXT : GOLD);
				const wchar_t* label = plain ? L"Play without Archipelago" : g_launcher ? L"Connect & Play" : L"Connect";
				DrawTextW(di->hDC, label, -1, &di->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				return TRUE;
			}
			case WM_SIZE:
				g_formTop = -1;
				InvalidateRect(hwnd, nullptr, FALSE);
				return 0;
			case WM_LBUTTONDOWN:
			{
				SetFocus(hwnd);
				POINT pt{ (short)LOWORD(lp) - g_sideX, (short)HIWORD(lp) };
				if ( PtInRect(&g_lootBox, pt) ) { g_lootShort = !g_lootShort; InvalidateRect(hwnd, nullptr, FALSE); }
				return 0;
			}
			case WM_MOUSEMOVE:
			{
				int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
				int tx = -1, ty = -1;
				if ( g_mapCell > 0 && mx >= g_mapX && my >= g_mapY )
				{
					tx = (mx - g_mapX) / g_mapCell;
					ty = (my - g_mapY) / g_mapCell;
					if ( tx >= g_map.w || ty >= g_map.h ) { tx = ty = -1; }
				}
				if ( tx != g_hoverX || ty != g_hoverY || mx != g_mouseX || my != g_mouseY )
				{
					bool redraw = tx != g_hoverX || ty != g_hoverY || pileAt(tx, ty) != nullptr;
					g_hoverX = tx; g_hoverY = ty; g_mouseX = mx; g_mouseY = my;
					if ( redraw ) { InvalidateRect(hwnd, nullptr, FALSE); }
				}
				return 0;
			}
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
				paint(hwnd);
				return 0;
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
		}
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show)
{
	g_launcher = strstr(GetCommandLineA(), "--launcher") != nullptr;
	// Only one tracker at a time. The launcher replaces an old tracker window that's still open.
	HANDLE mutex = CreateMutexA(nullptr, TRUE, "BaronyAPTrackerSingleInstance");
	if ( GetLastError() == ERROR_ALREADY_EXISTS )
	{
		if ( !g_launcher ) { return 0; }
		if ( HWND old = FindWindowW(L"BaronyAPTracker", nullptr) ) { PostMessageW(old, WM_CLOSE, 0, 0); }
		WaitForSingleObject(mutex, 3000);
	}

	Gdiplus::GdiplusStartupInput gdiplusInput;
	ULONG_PTR gdiplusToken = 0;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
	// Barony's title with the Archipelago logo (the game makes it at startup), else Barony's own.
	g_logo = loadImage("images\\ui\\Archipelago\\AP_Title.png");
	if ( !g_logo ) { g_logo = loadImage("images\\system\\title.png"); }

	// Barony's own UI font (Pixel Maz), from the game's fonts folder; Consolas if it can't be loaded.
	std::string fontPath = exeDir() + "\\fonts\\pixel_maz.ttf";
	if ( AddFontResourceExA(fontPath.c_str(), FR_PRIVATE, nullptr) > 0 )
	{
		g_titleFont = CreateFontW(-44, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"Pixel Maz");
		g_bodyFont = CreateFontW(-32, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"Pixel Maz");
	}
	else
	{
		g_titleFont = CreateFontW(26, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"Consolas");
		g_bodyFont = CreateFontW(17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
	}

	WNDCLASSW wc{};
	wc.lpfnWndProc = wndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = L"BaronyAPTracker";
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassW(&wc);

	HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Barony Archipelago Tracker",
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1300, 900, nullptr, nullptr, inst, nullptr);
	g_main = hwnd;
	g_editBrush = CreateSolidBrush(PANEL);
	auto makeEdit = [&](DWORD extra) {
		HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | extra,
			0, 0, 10, 10, hwnd, nullptr, inst, nullptr);
		SendMessageW(e, WM_SETFONT, (WPARAM)g_bodyFont, TRUE);
		return e;
	};
	g_editServer = makeEdit(0);
	g_editPort = makeEdit(ES_NUMBER);
	g_editName = makeEdit(0);
	g_btnConnect = CreateWindowExW(0, L"BUTTON", L"Connect", WS_CHILD | BS_OWNERDRAW,
		0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_CONNECT, inst, nullptr);
	g_btnPlain = CreateWindowExW(0, L"BUTTON", L"Play without Archipelago", WS_CHILD | BS_OWNERDRAW,
		0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_PLAIN, inst, nullptr);
	fillForm();
	ShowWindow(hwnd, show);
	SetTimer(hwnd, 1, 1000, nullptr);
	readState();
	readMap();

	MSG m;
	while ( GetMessageW(&m, nullptr, 0, 0) )
	{
		// In the connect form: Enter connects, Tab moves to the next box.
		if ( m.message == WM_KEYDOWN && (m.hwnd == g_editServer || m.hwnd == g_editPort || m.hwnd == g_editName) )
		{
			if ( m.wParam == VK_RETURN ) { doConnect(); continue; }
			if ( m.wParam == VK_TAB )
			{
				bool back = GetKeyState(VK_SHIFT) < 0;
				HWND order[] = { g_editServer, g_editPort, g_editName };
				int i = m.hwnd == g_editServer ? 0 : m.hwnd == g_editPort ? 1 : 2;
				SetFocus(order[(i + (back ? 2 : 1)) % 3]);
				SendMessageW(GetFocus(), EM_SETSEL, 0, -1);
				continue;
			}
		}
		if ( m.message == WM_CHAR && (m.wParam == '\r' || m.wParam == '\t')
			&& (m.hwnd == g_editServer || m.hwnd == g_editPort || m.hwnd == g_editName) ) { continue; } // no beep
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	delete g_logo;
	if ( g_editBrush ) { DeleteObject(g_editBrush); }
	Gdiplus::GdiplusShutdown(gdiplusToken);
	if ( mutex ) { CloseHandle(mutex); }
	return 0;
}
