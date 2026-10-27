// Barony Archipelago tracker window.
//
// Barony (the Archipelago build) starts this when it connects, and writes
// archipelago/tracker.txt about once a second. This window shows that file:
// connection, floor, what's left on the floor, lives, keys, and the item log.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; } // gdiplus.h needs these without the macros
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <fstream>
#include <map>
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

		if ( !g_state.fileFound )
		{
			text(dc, 14, y, L"Waiting for Barony to connect...", DIM, g_bodyFont);
		}
		else
		{
			bool connected = num("connected") != 0;
			std::wstring slot = widen(g_state.values["slot"]);
			text(dc, 14, y, connected ? L"Connected as " + slot : L"Not connected", connected ? GOOD : BAD, g_bodyFont);
			y += 26;

			if ( num("ingame") )
			{
				wchar_t buf[256];
				swprintf(buf, 256, L"Floor %d     Lives %d     Stair Keys %d", num("floor"), num("lives"), num("keys"));
				text(dc, 14, y, buf, TEXT, g_bodyFont);
				y += 24;
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
				text(dc, 20, y + 10, g_lootShort ? L"Other loot on this floor  (F4: more)" : L"Other loot on this floor", STEEL, g_bodyFont);
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
				InvalidateRect(hwnd, nullptr, FALSE);
				return 0;
			case WM_KEYDOWN:
				if ( wp == VK_TAB ) { g_showAll = !g_showAll; InvalidateRect(hwnd, nullptr, FALSE); }
				if ( wp == 'L' || wp == VK_F4 ) { g_lootShort = !g_lootShort; InvalidateRect(hwnd, nullptr, FALSE); }
				return 0;
			case WM_LBUTTONDOWN:
			{
				POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
				if ( PtInRect(&g_lootBox, pt) ) { g_lootShort = !g_lootShort; InvalidateRect(hwnd, nullptr, FALSE); }
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
	// Only one tracker at a time.
	HANDLE mutex = CreateMutexA(nullptr, TRUE, "BaronyAPTrackerSingleInstance");
	if ( GetLastError() == ERROR_ALREADY_EXISTS ) { return 0; }

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
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 470, 960, nullptr, nullptr, inst, nullptr);
	ShowWindow(hwnd, show);
	SetTimer(hwnd, 1, 1000, nullptr);
	readState();

	MSG m;
	while ( GetMessageW(&m, nullptr, 0, 0) )
	{
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	delete g_logo;
	Gdiplus::GdiplusShutdown(gdiplusToken);
	if ( mutex ) { CloseHandle(mutex); }
	return 0;
}
