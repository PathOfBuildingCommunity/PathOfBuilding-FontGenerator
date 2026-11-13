// Regression coverage for Cyrillic Notes spec clause 3.
// Run in a temporary working directory; the generator writes Generated Fonts there.
#include "../fontgen.cpp"
#include <iostream>

int main()
{
	HDC dc = CreateCompatibleDC(nullptr);
	HFONT font = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		VARIABLE_PITCH, L"Arial");
	auto previous = SelectObject(dc, font);
	glFontGenV2_c generator;
	generator.Init("CyrillicTest");
	generator.Build(dc);
	generator.Finish();
	SelectObject(dc, previous);
	DeleteObject(font);
	DeleteDC(dc);
	std::ifstream stream("Generated Fonts/CyrillicTest.tgf");
	std::string metadata((std::istreambuf_iterator<char>(stream)), {});
	int failures = 0;
	for (unsigned codepoint : { 0x401u, 0x410u, 0x44fu, 0x451u }) {
		bool present = metadata.find("GLYPH_U32 " + std::to_string(codepoint) + " ") != std::string::npos;
		std::cout << (present ? "PASS" : "FAIL") << ": exports Cyrillic codepoint " << codepoint << "\n";
		failures += !present;
	}
	std::cout << "4 tests, " << failures << " failures\n";
	return failures != 0;
}
