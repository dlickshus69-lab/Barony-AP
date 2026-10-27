// Barony Archipelago tracker window.
//
// Barony (the Archipelago build) starts this when it connects, and writes
// archipelago/tracker.txt about once a second. This window shows that file:
// connection, floor, what's left on the floor, lives, keys, and the item log.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
	HFONT g_titleFont = nullptr, g_bodyFont = nullptr;

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

		int y = 12;
		text(dc, 14, y, L"Barony Archipelago", GOLD, g_titleFont);
		y += 34;

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
				y += 30;

				RECT panel{ 10, y, rc.right - 10, y + 118 };
				fill(dc, panel, PANEL);
				int ground = num("ground"), chests = num("chests"), shop = num("shop"), marked = num("marked");
				int total = ground + chests + shop + marked;
				text(dc, 20, y + 8, L"Left on this floor", GOLD, g_bodyFont);
				swprintf(buf, 256, L"%d", total);
				text(dc, rc.right - 60, y + 8, buf, total ? GOLD : DIM, g_titleFont);
				swprintf(buf, 256, L"On the ground: %d", ground);
				text(dc, 30, y + 36, buf, ground ? TEXT : DIM, g_bodyFont);
				swprintf(buf, 256, L"In chests: %d", chests);
				text(dc, 30, y + 56, buf, chests ? TEXT : DIM, g_bodyFont);
				swprintf(buf, 256, L"In the shop: %d", shop);
				text(dc, 30, y + 76, buf, shop ? TEXT : DIM, g_bodyFont);
				swprintf(buf, 256, L"Green enemies: %d", marked);
				text(dc, 30, y + 96, buf, marked ? TEXT : DIM, g_bodyFont);
				y += 128;
				swprintf(buf, 256, L"Archipelago pickups still to find: %d", num("pickups_left"));
				text(dc, 14, y, buf, DIM, g_bodyFont);
				y += 30;
			}
			else
			{
				text(dc, 14, y, L"Start a run to see your floor.", DIM, g_bodyFont);
				y += 30;
			}

			{
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
				y += 10;
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
				return 0;
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

	g_titleFont = CreateFontW(26, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"Consolas");
	g_bodyFont = CreateFontW(17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");

	WNDCLASSW wc{};
	wc.lpfnWndProc = wndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = L"BaronyAPTracker";
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassW(&wc);

	HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Barony Archipelago Tracker",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 460, 640, nullptr, nullptr, inst, nullptr);
	ShowWindow(hwnd, show);
	SetTimer(hwnd, 1, 1000, nullptr);
	readState();

	MSG m;
	while ( GetMessageW(&m, nullptr, 0, 0) )
	{
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	if ( mutex ) { CloseHandle(mutex); }
	return 0;
}
