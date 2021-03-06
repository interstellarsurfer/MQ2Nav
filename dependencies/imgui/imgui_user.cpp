
// stupid hack to make visual studio happy. This is defined in imgui.cpp
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.cpp"
#include "imgui.h"
#include "imgui_internal.h"
#endif

static void RenderTextOverlay(ImVec2 pos, const ImVec4& color, ImGuiAlign_ alignment,
	const char* text, const char* text_end)
{
	ImGuiState& g = *GImGui;

	if (!text_end)
		text_end = text + strlen(text); // FIXME-OPT
	const char* text_display_end = text_end;

	// if the position is negative, then make it relative to the opposite end of the screen
	if (pos.y < 0)
		pos.y = ImGui::GetIO().DisplaySize.y + pos.y;
	if (pos.x < 0)
		pos.x = ImGui::GetIO().DisplaySize.x + pos.x;

	const int text_len = (int)(text_display_end - text);

	if (text_len > 0)
	{
		if (alignment == ImGuiAlign_Center)
		{
			float displayWidth = ImGui::GetIO().DisplaySize.x;

			// if centered, figure out the text width. We want to wrap at screen edges, so use
			// the screen width minus the distance from center as the available width.
			float availableWidth = displayWidth - ((displayWidth / 2) - pos.x);

			ImVec2 size = ImGui::CalcTextSize(text, text_end, false, availableWidth);

			// subtract half of the size from the x position to center text at that point.
			pos.x -= size.x / 2;
		}
		else if (alignment == ImGuiAlign_Right)
		{
			// if right aligned, then subtract width of the string from the position.
			ImVec2 size = ImGui::CalcTextSize(text, text_end, false);

			// subtract half of the size from the x position to center text at that point.
			pos.x -= size.x;
		}

		g.OverlayDrawList.AddText(g.Font, g.FontSize, pos, ImGui::ColorConvertFloat4ToU32(color),
			text, text_end);
	}
}

void ImGui::RenderText(const ImVec2& pos, const ImVec4& color, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	ImGuiState& g = *GImGui;
	const char* text_end = g.TempBuffer + ImFormatStringV(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), fmt, args);

	RenderTextOverlay(pos, color, ImGuiAlign_Left, g.TempBuffer, text_end);
}

void ImGui::RenderText(int x, int y, const ImVec4& color, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	ImGuiState& g = *GImGui;
	const char* text_end = g.TempBuffer + ImFormatStringV(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), fmt, args);

	RenderTextOverlay(ImVec2((float)x, (float)y), color, ImGuiAlign_Left, g.TempBuffer, text_end);
}

void ImGui::RenderTextCentered(const ImVec2& pos, const ImVec4& color, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	ImGuiState& g = *GImGui;
	const char* text_end = g.TempBuffer + ImFormatStringV(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), fmt, args);

	RenderTextOverlay(pos, color, ImGuiAlign_Center, g.TempBuffer, text_end);
}

void ImGui::RenderTextCentered(int x, int y, const ImVec4& color, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	ImGuiState& g = *GImGui;
	const char* text_end = g.TempBuffer + ImFormatStringV(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), fmt, args);

	RenderTextOverlay(ImVec2((float)x, (float)y), color, ImGuiAlign_Center, g.TempBuffer, text_end);
}

void ImGui::RenderTextRight(const ImVec2& pos, const ImVec4& color, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	ImGuiState& g = *GImGui;
	const char* text_end = g.TempBuffer + ImFormatStringV(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), fmt, args);

	RenderTextOverlay(pos, color, ImGuiAlign_Right, g.TempBuffer, text_end);
}

void ImGui::RenderTextRight(int x, int y, const ImVec4& color, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	ImGuiState& g = *GImGui;
	const char* text_end = g.TempBuffer + ImFormatStringV(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), fmt, args);

	RenderTextOverlay(ImVec2((float)x, (float)y), color, ImGuiAlign_Right, g.TempBuffer, text_end);
}

float ImGui::ProgressBar(const char *optionalPrefixText, float value, const float minValue, const float maxValue,
	const char *format, const ImVec2 &sizeOfBarWithoutTextInPixels, const ImVec4 &colorLeft, const ImVec4 &colorRight,
	const ImVec4 &colorBorder)
{
	if (value<minValue) value = minValue;
	else if (value>maxValue) value = maxValue;
	const float valueFraction = (maxValue == minValue) ? 1.0f : ((value - minValue) / (maxValue - minValue));
	const bool needsPercConversion = strstr(format, "%%") != NULL;

	ImVec2 size = sizeOfBarWithoutTextInPixels;
	if (size.x <= 0) size.x = ImGui::GetWindowWidth()*0.25f;
	if (size.y <= 0) size.y = ImGui::GetTextLineHeightWithSpacing(); // or without

	const ImFontAtlas* fontAtlas = ImGui::GetIO().Fonts;

	if (optionalPrefixText && strlen(optionalPrefixText) > 0) {
		ImGui::AlignFirstTextHeightToWidgets();
		ImGui::Text(optionalPrefixText);
		ImGui::SameLine();
	}

	if (valueFraction > 0) {
		ImGui::Image(fontAtlas->TexID, ImVec2(size.x*valueFraction, size.y), fontAtlas->TexUvWhitePixel, fontAtlas->TexUvWhitePixel, colorLeft, colorBorder);
	}
	if (valueFraction < 1) {
		if (valueFraction > 0) ImGui::SameLine(0, 0);
		ImGui::Image(fontAtlas->TexID, ImVec2(size.x*(1.f - valueFraction), size.y), fontAtlas->TexUvWhitePixel, fontAtlas->TexUvWhitePixel, colorRight, colorBorder);
	}
	ImGui::SameLine();

	ImGui::Text(format, needsPercConversion ? (valueFraction*100.f + 0.0001f) : value);
	return valueFraction;
}