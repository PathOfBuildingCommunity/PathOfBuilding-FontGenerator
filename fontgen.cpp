#define _WIN32_WINNT 0x5000
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "comctl32.lib")

#include "resource.h"

// =====
// Types
// =====

// Targa file header
#pragma pack(push,1)
typedef struct {
	char 	id, cm, it;
	short 	cmi, cml;
	char 	cmb;
	short 	xo, yo, xs, ys;
	char 	pd, de;
} tgaHeader_s;
#pragma pack(pop)

// ========================
// OpenGL Font Generator V2
// ========================

// Glyph data
struct glyphDat_s {
	unsigned x, y;
	unsigned width;
	int spleft;
	int spright;

	std::unique_ptr<uint8_t[]> dat;
};

// Font height info
struct fontHeight_s {
	unsigned height;
	std::vector<glyphDat_s> glyphs;
};

class glFontGenV2_c {
public:
	void	Init(const std::string_view fontName);
	void	Build(HDC dc, bool italic = false, bool bold = false);
	void	Finish(bool italic = false, bool bold = false);

private:
	std::string fontName;
	std::filesystem::path outputDir;
	std::vector< fontHeight_s > heights;
};

void glFontGenV2_c::Init(const std::string_view i_fontName)
{
	fontName = i_fontName;
	outputDir = std::filesystem::path("Generated Fonts");
	std::error_code ec;
	std::filesystem::create_directories(outputDir, ec);
}

void glFontGenV2_c::Build(HDC hdc, bool italic, bool bold)
{
	// Get metrics
	TEXTMETRIC tm;
	GetTextMetrics(hdc, &tm);
	const unsigned height = tm.tmHeight;
	const unsigned cellPadding = height >> 2;

	auto& current_height = heights.emplace_back();
	current_height.height = height;

	for (unsigned k = 0; k < 128; k++) {
		// Get glyph
		GLYPHMETRICS gm;
		const MAT2 m2i = { 0, 1, 0, 0, 0, 0, 0, 1 };
		const unsigned bbSize = GetGlyphOutline(hdc, k, GGO_GRAY8_BITMAP, &gm, 0, NULL, &m2i);
		if (bbSize < 0) {
			return;
		}
		std::unique_ptr<uint8_t[]> bb = std::make_unique<uint8_t[]>(bbSize);
		GetGlyphOutline(hdc, k, GGO_GRAY8_BITMAP, &gm, bbSize, bb.get(), &m2i);

		// Get glyph info
		const unsigned xs = gm.gmBlackBoxX;
		const unsigned ys = gm.gmBlackBoxY;
		const int xo = gm.gmptGlyphOrigin.x;
		const int yo = tm.tmAscent - gm.gmptGlyphOrigin.y;
		const int glw = gm.gmCellIncX;

		// Initialise glyph data
		auto& glyph = current_height.glyphs.emplace_back();
		glyph.width = xs;
		glyph.spleft = xo;
		glyph.spright = glw - (xo + xs);
		glyph.dat = std::make_unique<uint8_t[]>(height * xs);
		memset(glyph.dat.get(), 0, height * xs);

			// Copy glyph image
			if (bbSize && ys > 0 && xs > 0) {
				unsigned rowStride = ((xs + 3u) & ~3u);
				if (rowStride == 0 || rowStride * ys > bbSize) {
					rowStride = bbSize / ys;
				}
				if (rowStride == 0) {
					rowStride = xs;
				}

				for (unsigned y = 0; y < ys; y++) {
					const int destRow = static_cast<int>(y) + yo;
					if (destRow < 0 || destRow >= static_cast<int>(height)) {
						continue;
					}

					const size_t srcOffset = static_cast<size_t>(y) * rowStride;
					if (srcOffset + xs > bbSize) {
						break;
					}
					const uint8_t* srcRow = bb.get() + srcOffset;
					uint8_t* dstRow = glyph.dat.get() + static_cast<size_t>(destRow) * xs;

					for (unsigned x = 0; x < xs; x++) {
						const uint8_t raw = srcRow[x];
						float a = std::clamp(raw / 64.0f, 0.0f, 1.0f);
						a = powf(a, 0.5f);
						dstRow[x] = static_cast<uint8_t>(a * 255.0f);
					}
				}
			}
		}

	const unsigned cellHeight = height + cellPadding;

	// Find best image size
	unsigned imgWidth = 0, imgHeight = 0;
	unsigned bestDim = std::numeric_limits<unsigned>::max();
	for (unsigned xmax = 32; xmax <= 2048; xmax <<= 1) {
		// Find number of rows for this width
		unsigned numRow = 1;
		unsigned rowWidth = 0;
		for (size_t g = 0; g < current_height.glyphs.size(); g++) {
			unsigned cellWidth = current_height.glyphs[g].width + cellPadding;
			if (cellWidth > xmax) {
				numRow = 999;
				break;
			}
			if (rowWidth + cellWidth <= xmax) {
				rowWidth += cellWidth;
			}
			else {
				rowWidth = 0;
				numRow++;
				g--;
			}
		}
		unsigned ymax = 1;
		while (ymax < numRow * cellHeight) ymax <<= 1;
		unsigned dim = (xmax > ymax) ? xmax : ymax;
		if (dim < bestDim) {
			imgWidth = xmax;
			imgHeight = ymax;
			bestDim = dim;
		}
		if (ymax < xmax) {
			break;
		}
	}

	// Prepare RGBA image, and fill with 255,255,255,0
	std::unique_ptr<uint32_t[]> image = std::make_unique<uint32_t[]>(imgWidth * imgHeight);
	std::fill_n(image.get(), imgWidth * imgHeight, 0x00FFFFFF);

	// Write the glyphs into the alpha channel of the image
	unsigned ix = 0, iy = 0;
	for (auto& glyph : current_height.glyphs) {
		const unsigned cellWidth = glyph.width + cellPadding;
		if (ix + cellWidth > imgWidth) {
			ix = 0;
			iy += cellHeight;
		}
		glyph.x = ix;
		glyph.y = iy;
		for (unsigned gy = 0; gy < height; gy++) {
			for (unsigned gx = 0; gx < glyph.width; gx++) {
				image[(iy + gy) * imgWidth + ix + gx] = 0x00FFFFFF + (glyph.dat[gy * glyph.width + gx] << 24);
			}
		}
		ix += cellWidth;
	}

	// Open image file for writing
	std::string tgaFileName = fontName;
	if (italic) tgaFileName += " Italic";
	if (bold) tgaFileName += " Bold";
	tgaFileName += "." + std::to_string(height) + ".tga";

	const auto tgaPath = outputDir / tgaFileName;
	std::ofstream out(tgaPath, std::ios_base::binary);
	if (!out) {
		return;
	}

	const auto WriteRaw = [&](const auto& val) { out.write((const char*)&val, sizeof(val)); };

	// Write header
	tgaHeader_s th;
	ZeroMemory(&th, sizeof(th));
	th.it = 10;
	th.pd = 32;
	th.xs = imgWidth;
	th.ys = imgHeight;
	WriteRaw(th);

	uint8_t hdr = 255;
	std::array<uint32_t, 129> packet;
	const auto WritePacket = [&]() {
		WriteRaw(hdr);
		if (hdr & 0x80)
			WriteRaw(packet[0]);
		else
			out.write((const char*)packet.data(), 4 * (hdr + 1));
		};	
		
	// Write image
	for (int y = imgHeight - 1; y >= 0; y--) {
		hdr = 255;
		uint32_t lastPix = 0;
		const uint32_t* line_start = &image[y * imgWidth];
		for (const auto* p = line_start; p < line_start + imgWidth; ++p) {
			if (hdr == 255) {
				// Start new packet
				hdr = 0;
				packet[hdr] = *p;
			}
			else if (hdr & 0x80) {
				// Run-length packet, check for continuance
				if (*p == lastPix) {
					hdr++;
					if (hdr == 255) {
						// Max length, write it
						WritePacket();
					}
				}
				else {
					WritePacket();
					hdr = 0;
					packet[0] = *p;
				}
			}
			else if (hdr) {
				// Raw packet, check if a run-length packet could be created with the last pixel
				if (*p == lastPix) {
					hdr--;
					WritePacket();
					packet[0] = *p;
					hdr = 129;
				}
				else if (hdr == 127) {
					// Packet is already full, write it
					WritePacket();
					hdr = 0;
					packet[hdr] = *p;
				}
				else {
					hdr++;
					packet[hdr] = *p;
				}
			}
			else {
				// New packet, check if this could become a run-length packet
				if (*p == lastPix) {
					hdr = 129;
				}
				else {
					hdr = 1;
					packet[hdr] = *p;
				}
			}
			lastPix = *p;
		}
		if (hdr < 255) {
			// Leftover packet, write it
			WritePacket();
		}
	}
}

void glFontGenV2_c::Finish(bool italic, bool bold)
{
	// Open info file for writing
	std::string tgfFileName = fontName;
	if (italic) tgfFileName += " Italic";
	if (bold) tgfFileName += " Bold";
	tgfFileName += ".tgf";

	const auto tgfPath = outputDir / tgfFileName;
	std::ofstream tgf(tgfPath);
	if (!tgf) {
		return;
	}

	for (const auto& height_data : heights) {
		// Write glyph attributes
		tgf << "HEIGHT " << height_data.height << ";\n";
		for (size_t g = 0; g < height_data.glyphs.size(); g++) {
			const auto& glyph = height_data.glyphs[g];
			tgf << "GLYPH " << std::setw(3) << glyph.x << " " << std::setw(3)
				<< glyph.y << " " << std::setw(2) << glyph.width << " "
				<< std::setw(2) << glyph.spleft << " " << std::setw(2)
				<< glyph.spright << ";\t// " << g;
			if (isprint((int)g)) {
				tgf << " (" << (char)g << ")";
			}
			tgf << "\n";
		}
	}

	heights.clear();
}

// ===============
// Main Dialog Box
// ===============

static void BuildFont(HDC hdc, char* fontName, int size, int fontWeight, int fontPitch, BOOL italic, BOOL bold, glFontGenV2_c& gen)
{
	// Create font
	HFONT hf = CreateFont(
		size, 0, 0, 0,
		bold ? FW_BOLD : fontWeight, italic, FALSE, FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		fontPitch | FF_MODERN, fontName
	);
	if (hf == NULL) {
		return;
	}
	// Select the font
	HGDIOBJ prevf = SelectObject(hdc, hf);

	// Run font generator
	gen.Build(hdc, italic, bold);

	// Delete font object
	SelectObject(hdc, prevf);
	DeleteObject(hf);
}

static void BuildSelectedFonts(HWND hwndDlg)
{
	// Get font name
	char fontName[64] = {};
	HWND hList = GetDlgItem(hwndDlg, IDC_FONTSEL);
	int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
	if (sel == LB_ERR) {
		return;
	}
	SendMessage(hList, LB_GETTEXT, sel, (LPARAM)fontName);
	if (*fontName == 0) {
		return;
	}

	// Get font attributes
	int fontWeight = GetDlgItemInt(hwndDlg, IDC_FONTWEIGHT, NULL, 0);
	int fontPitch = (Button_GetCheck(GetDlgItem(hwndDlg, IDC_FONTFIXED))) ? FIXED_PITCH : VARIABLE_PITCH;
	BOOL italic = Button_GetCheck(GetDlgItem(hwndDlg, IDC_FONTITALIC));
	BOOL bold = Button_GetCheck(GetDlgItem(hwndDlg, IDC_FONTBOLD));

	if (bold && fontWeight < FW_BOLD)
		fontWeight = FW_BOLD;

	glFontGenV2_c gen;
	gen.Init(fontName);

	// Generate selected fonts
	HDC hdc = GetDC(hwndDlg);

	
	if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZALL))) {
		const int allSizes[] = { 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 32, 36, 40, 48, 56, 64 };
		for (int size : allSizes)
			BuildFont(hdc, fontName, size, fontWeight, fontPitch, italic, bold, gen);
	}
	else {

		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ10))) BuildFont(hdc, fontName, 10, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ12))) BuildFont(hdc, fontName, 12, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ14))) BuildFont(hdc, fontName, 14, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ16))) BuildFont(hdc, fontName, 16, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ18))) BuildFont(hdc, fontName, 18, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ20))) BuildFont(hdc, fontName, 20, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ22))) BuildFont(hdc, fontName, 22, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ24))) BuildFont(hdc, fontName, 24, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ26))) BuildFont(hdc, fontName, 26, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ28))) BuildFont(hdc, fontName, 28, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ32))) BuildFont(hdc, fontName, 32, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ36))) BuildFont(hdc, fontName, 36, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ40))) BuildFont(hdc, fontName, 40, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ48))) BuildFont(hdc, fontName, 48, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ56))) BuildFont(hdc, fontName, 56, fontWeight, fontPitch, italic, bold, gen);
		if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_SZ64))) BuildFont(hdc, fontName, 64, fontWeight, fontPitch, italic, bold, gen);
	}

	gen.Finish(italic, bold);

	MessageBox(hwndDlg, "Font generated successfully.", "OpenGL Font Generator", MB_ICONINFORMATION);
}

// EnumFontProc: add names to the LISTBOX instead of combo
static int __stdcall EnumFontProc(const LOGFONT* lf, const TEXTMETRIC* ltm, unsigned long FontType, LPARAM lParam)
{
	HWND hList = (HWND)lParam;
	// SendMessage will pick ANSI/UNICODE variant automatically
	SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)lf->lfFaceName);
	return 1;
}

static HFONT g_hPreviewFont = NULL;

// Create or update the preview control font and text.
// Uses current selection, weight and italic settings to create a sample HFONT
static void UpdatePreview(HWND hwndDlg)
{
	// Get selected font name from listbox
	char fontName[128] = {};
	HWND hList = GetDlgItem(hwndDlg, IDC_FONTSEL);
	int sel = (int)SendMessageA(hList, LB_GETCURSEL, 0, 0);
	if (sel == LB_ERR) return;
	SendMessageA(hList, LB_GETTEXT, sel, (LPARAM)fontName);
	if (fontName[0] == '\0') return;

	// Get preview text from edit control
	char previewText[512] = {};
	GetDlgItemTextA(hwndDlg, IDC_PREVIEWTEXT, previewText, (int)std::size(previewText));
	if (previewText[0] == '\0') {
		strcpy_s(previewText, "The quick brown fox jumps over the lazy dog");
		SetDlgItemTextA(hwndDlg, IDC_PREVIEWTEXT, previewText);
	}

	// Get weight, italic, and bold
	int fontWeight = GetDlgItemInt(hwndDlg, IDC_FONTWEIGHT, NULL, FALSE);
	BOOL italic = Button_GetCheck(GetDlgItem(hwndDlg, IDC_FONTITALIC));
	BOOL bold = Button_GetCheck(GetDlgItem(hwndDlg, IDC_FONTBOLD));

	// Choose a reasonable preview point size (you can change this)
	const int previewPoint = 18;

	// Convert points -> device pixels and request character height
	HDC hdc = GetDC(hwndDlg);
	const int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
	ReleaseDC(hwndDlg, hdc);
	const int lfHeight = -MulDiv(previewPoint, dpiY, 72);

	// Build LOGFONT
	LOGFONTA lf;
	memset(&lf, 0, sizeof(lf));
	lf.lfHeight = lfHeight;
	lf.lfWeight = bold ? FW_BOLD : ((fontWeight > 0) ? fontWeight : FW_NORMAL);
	lf.lfItalic = italic ? 1 : 0;
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfPitchAndFamily = FF_DONTCARE;
	strncpy_s(lf.lfFaceName, fontName, _TRUNCATE);

	// Create HFONT
	HFONT hNewFont = CreateFontIndirectA(&lf);
	if (!hNewFont) return;

	// Apply to preview control
	HWND hPreview = GetDlgItem(hwndDlg, IDC_PREVIEW);
	// Set text first then font so control reports correct layout
	SetWindowTextA(hPreview, previewText);
	SendMessageA(hPreview, WM_SETFONT, (WPARAM)hNewFont, TRUE);

	// Destroy previous font
	if (g_hPreviewFont) {
		// safe to delete after setting new font (control now uses hNewFont)
		DeleteObject(g_hPreviewFont);
	}
	g_hPreviewFont = hNewFont;
}

static int __stdcall DlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CLOSE:
		if (g_hPreviewFont) { DeleteObject(g_hPreviewFont); g_hPreviewFont = NULL; }
		EndDialog(hwndDlg, 1);
		break;

	case WM_INITDIALOG:
		// Add font weights
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "100");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "200");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "300");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "400");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "500");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "600");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "700");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "800");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "900");
		ComboBox_AddString(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), "1000");
		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_FONTWEIGHT), 4);

		// Add font names
		EnumFontFamilies(GetDC(hwndDlg), NULL, EnumFontProc, (LPARAM)GetDlgItem(hwndDlg, IDC_FONTSEL));
		ListBox_SetCurSel(GetDlgItem(hwndDlg, IDC_FONTSEL), 0);

		// Default preview text
		SetDlgItemTextA(hwndDlg, IDC_PREVIEWTEXT, "Path of Exile 2");
		// Check the "All" box by default
		CheckDlgButton(hwndDlg, IDC_SZALL, BST_CHECKED);
		// Initialize preview
		UpdatePreview(hwndDlg);

		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_GEN:
			BuildSelectedFonts(hwndDlg);
			break;
		case IDC_QUIT:
			// cleanup preview font
			if (g_hPreviewFont) { DeleteObject(g_hPreviewFont); g_hPreviewFont = NULL; }
			EndDialog(hwndDlg, 1);
			break;
		case IDC_FONTSEL:
		case IDC_FONTITALIC:
		case IDC_FONTBOLD:
		case IDC_FONTWEIGHT:
		case IDC_PREVIEWTEXT:
			if (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == LBN_SELCHANGE || HIWORD(wParam) == EN_CHANGE)
				UpdatePreview(hwndDlg);
			break;
		}
		break;

	default:
		return 0;
	}
	return 1;
}

int __stdcall WinMain(HINSTANCE hInst, HINSTANCE, char*, int)
{
	InitCommonControls();
	return DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_MAIN), 0, DlgProc, 0);
}
