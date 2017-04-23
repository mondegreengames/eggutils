#include <cstdio>
#include "SDL.h"
#include "GL/glew.h"

#include "noc_file_dialog.h"
#include "egg.h"

#include "FileSystem.h"

#define ENABLE_SRGB

#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg.h"
#include "nanovg_gl.h"

bool g_quitRequested = false;
int g_screenWidth = 640;
int g_screenHeight = 480;

struct MenuItem
{
	const char* Text;

	int NumChildren;
	MenuItem* Children;
};

struct Context
{
	NVGcontext* NVG;

	int MouseX, MouseY, MouseWheel;
	bool LButtonDown;
	int LButtonTransitionCount;

	void* Hot;
	void* Modal;

	float Scroll;

	const char* MsgBoxMessage;

	bool Invalidated;
};

MenuItem file[] = {
	{ "Open", 0, nullptr },
	{ "Exit", 0, nullptr }
};

MenuItem help[] = {
	{ "About", 0, nullptr }
};

MenuItem menu[] = {
	{ "File", 2, file },
	{ "Tools", 0, nullptr },
	{ "Help", 1, help }
};


NVGcolor srgb(unsigned char r, unsigned char g, unsigned char b)
{
#ifdef ENABLE_SRGB
	float fr = powf((float)r / 255.0f, 2.2f);
	float fg = powf((float)g / 255.0f, 2.2f);
	float fb = powf((float)b / 255.0f, 2.2f);

	r = (unsigned char)(fr * 255);
	g = (unsigned char)(fg * 255);
	b = (unsigned char)(fb * 255);
#endif

	return nvgRGB(r, g, b);
}

struct ScrollData
{
	float Value;
	float MaxValue;
};

struct ListBoxRow
{
	const char** Items;
};

struct ListBoxData
{
	int NumColumns;
	const char** HeaderNames;
	int* ColumnWidths;

	int NumRows;
	ListBoxRow* Rows;

	int FirstVisibleRow;
	int SelectedRowIndex;

	ScrollData Scrolling;
};

ListBoxData data;

void menuClicked(Context* c, MenuItem* item)
{
	if (item == &file[0])
	{
		const char* result = noc_file_dialog_open(NOC_FILE_DIALOG_OPEN, nullptr, nullptr, nullptr);
		if (result)
		{
			File f;
			if (FileSystem::Open(result, &f) == false)
				c->MsgBoxMessage = "Unable to open file :(";
			else
			{
				auto bytes = FileSystem::MapFile(&f);
				if (bytes == nullptr)
				{
					c->MsgBoxMessage = "Unable to map file";
					FileSystem::Close(&f);
					return;
				}
				megg_info egg;
				if (megg_getEggInfo((unsigned char*)f.Memory, f.FileSize, &egg) != 0)
				{
					c->MsgBoxMessage = "Unable to read egg archive";
					FileSystem::Close(&f);
					return;
				}

				data.NumRows = egg.NumFiles;
				data.Rows = new ListBoxRow[egg.NumFiles];
				auto fileCursor = egg.Filenames;
				for (unsigned int i = 0; i < egg.NumFiles; i++)
				{
					data.Rows[i].Items = new const char*[4];
					data.Rows[i].Items[0] = new char[fileCursor->Length + 1];
					strcpy((char*)data.Rows[i].Items[0], fileCursor->Name);

					data.Rows[i].Items[1] = new char[10];
					_itoa(egg.TableOfContents[i].CompressedSize, (char*)data.Rows[i].Items[1], 10);

					data.Rows[i].Items[2] = nullptr;

					if (egg.TableOfContents[i].Flags == 1)
						data.Rows[i].Items[3] = "LZ4";
					else
						data.Rows[i].Items[3] = "None";

					fileCursor += fileCursor->Length + 2;
				}
				FileSystem::Close(&f);
			}
		}
	}
	else if (item == &file[1])
		g_quitRequested = true;
	else if (item == &help[0])
		c->MsgBoxMessage = "EGG archive browser v0.0.1";
}

const float textOffsetX = 5.0f;

void drawBox(NVGcontext* ctx, float centerX, float centerY, float width, float height, NVGcolor color, float originX = 0.5f, float originY = 0.5f)
{
	float x = roundf(centerX - width * originX);
	float y = roundf(centerY - height * originY);

	nvgBeginPath(ctx);
	nvgFillColor(ctx, color);
	nvgRect(ctx, x, y, width, height);
	nvgFill(ctx);
}


void drawVScroll(Context* c, float x, float y, float h, ScrollData* scroll)
{
	const float scrubberSize = 50.0f;
	float scrollPercent = scroll->Value / scroll->MaxValue;

	// background
	const float width = 10.0f;
	drawBox(c->NVG, x, y, width, h, srgb(40, 40, 40), 0, 0);

	// bar
	drawBox(c->NVG, x, y + (h - scrubberSize) * scrollPercent, width, 50, srgb(100, 100, 100), 0, 0);

	float scrubberY = y + (h - scrubberSize) * scrollPercent;

	if (c->Modal == nullptr)
	{
		if (c->MouseX >= x && c->MouseX <= x + width &&
			c->MouseY >= scrubberY && c->MouseY <= scrubberY + scrubberSize)
		{
			if (c->LButtonDown == true && c->LButtonTransitionCount > 0)
			{
				c->Hot = scroll;
				c->Scroll = c->MouseY - scrubberY;
			}
		}

		// scrolling?
		if (c->Hot == scroll && c->LButtonDown)
		{
			scrubberY = c->MouseY - c->Scroll;

			scrollPercent = (scrubberY - y) / (h - scrubberSize);

			scroll->Value = scroll->MaxValue * scrollPercent;
		}
	}
}

void doListbox(Context* c, ListBoxData* data, float x, float y, float w, float h)
{	
	int maxVisibleRows = (int)((h - 30) / 30);

	data->FirstVisibleRow -= c->MouseWheel;
	if (data->FirstVisibleRow > data->NumRows - maxVisibleRows)
		data->FirstVisibleRow = data->NumRows - maxVisibleRows;
	if (data->FirstVisibleRow < 0)
		data->FirstVisibleRow = 0;

	float ascender, descender, height;
	nvgTextMetrics(c->NVG, &ascender, &descender, &height);

	if (c->Modal == nullptr && c->LButtonDown == false && c->LButtonTransitionCount > 0)
	{
		if (c->MouseX >= x && c->MouseX <= x + w &&
			c->MouseY >= y && c->MouseY <= y + h)
		{
			// the box was clicked. Determine which row.
			c->Hot = data;

			int row = (int)((c->MouseY - y) / 30);
			if (row > 0)
			{
				data->SelectedRowIndex = data->FirstVisibleRow + row - 1;
			}
		}
	}

	float textOffsetY = 30 * 0.5f + ascender * 0.5f;

	// draw the background
	drawBox(c->NVG, x, y, w, h, srgb(80, 80, 80), 0, 0);

	// draw the header
	drawBox(c->NVG, x, y, w, 30, srgb(100, 100, 100), 0, 0);

	// draw the header text
	float cx = 0;
	nvgBeginPath(c->NVG);
	nvgFillColor(c->NVG, srgb(255, 255, 255));
	for (int i = 0; i < data->NumColumns; i++)
	{
		nvgText(c->NVG, cx, y + textOffsetY, data->HeaderNames[i], nullptr);

		cx += (float)data->ColumnWidths[i];
	}

	// draw each row
	float cy = y + 30.0f;
	for (int i = data->FirstVisibleRow; i < data->NumRows; i++)
	{
		cx = x;
		
		if (i == data->SelectedRowIndex)
		{
			drawBox(c->NVG, x, cy, w, 30, srgb(0, 50, 90), 0, 0);
			//nvgBeginPath(c->NVG);
			//nvgFillColor(c->NVG, srgb(0, 50, 90));
			//nvgRect(ctx, x, cy, w, 30);
			//nvgFill(ctx);
			nvgBeginPath(c->NVG);
		}
		
		nvgFillColor(c->NVG, srgb(255, 255, 255));

		for (int j = 0; j < data->NumColumns; j++)
		{
			if (data->Rows[i].Items[j] != nullptr)
				nvgText(c->NVG, cx, cy + textOffsetY, data->Rows[i].Items[j], nullptr);

			cx += data->ColumnWidths[j];
		}

		cy += 30;
	}



	// draw the scrollbar
	data->Scrolling.Value = (float)data->FirstVisibleRow;
	data->Scrolling.MaxValue = (float)(data->NumRows - maxVisibleRows);
	drawVScroll(c, (float)(g_screenWidth - 10), 30, (float)(g_screenHeight - 30), &data->Scrolling);

	data->FirstVisibleRow = (int)data->Scrolling.Value;
}

MenuItem* doMenu(Context* c, int font, MenuItem* items, int numItems, float x, float y)
{
	float ascender, descender, height;
	nvgTextMetrics(c->NVG, &ascender, &descender, &height);

	float textOffsetY = 30 * 0.5f + ascender * 0.5f;

	drawBox(c->NVG, x, y, 50, numItems * 30.0f, srgb(40, 40, 40), 0, 0);

	MenuItem* clickedItem = nullptr;

	nvgBeginPath(c->NVG);
	nvgFillColor(c->NVG, srgb(255, 255, 255));
	for (int j = 0; j < numItems; j++)
	{
		nvgText(c->NVG, textOffsetX + x, y + j * 30 + textOffsetY, items[j].Text, nullptr);

		if (c->Modal == nullptr &&
			c->MouseX >= x && c->MouseX < x + 50 &&
			c->MouseY >= y + j * 30 && c->MouseY <= y + (j + 1) * 30 &&
			c->LButtonDown && c->LButtonTransitionCount > 0)
		{
			c->Hot = nullptr;
			clickedItem = &items[j];
		}
	}

	return clickedItem;
}

MenuItem* doMenuBar(Context* c, int font, MenuItem* items, int numItems)
{
	nvgFontFaceId(c->NVG, font);

	// draw menu background
	drawBox(c->NVG, 0, 0, (float)g_screenWidth, 30, srgb(50, 50, 50), 0, 0);
	
	float ascender, descender, height;
	nvgTextMetrics(c->NVG, &ascender, &descender, &height);

	float textOffsetY = 30 * 0.5f + ascender * 0.5f;

	MenuItem* clickedItem = nullptr;
	for (int i = 0; i < 3; i++)
	{
		if (c->MouseX >= i * 50 && c->MouseX < (i + 1) * 50 &&
			c->MouseY >= 0 && c->MouseY <= 30)
		{
			drawBox(c->NVG, (float)(i * 50), 0, 50, 30, srgb(60, 60, 60), 0, 0);

			if (c->Modal == nullptr && c->LButtonDown == false && c->LButtonTransitionCount > 0)
			{
				if (c->Hot == &menu[i])
				{
					c->Hot = nullptr;
				}
				else
				{
					c->Hot = &menu[i];
				}

				c->Invalidated = true;
			}
		}

		nvgBeginPath(c->NVG);
		if (c->Hot == &menu[i])
			nvgFillColor(c->NVG, srgb(255, 0, 0));
		else
			nvgFillColor(c->NVG, srgb(255, 255, 255));
		nvgText(c->NVG, textOffsetX + i * 50, textOffsetY, menu[i].Text, nullptr);

		if (c->Hot == &menu[i])
		{
			auto item = doMenu(c, font, menu[i].Children, menu[i].NumChildren, i * 50.0f, 30);
			if (item != nullptr)
				clickedItem = item;
		}
	}

	return clickedItem;
}

bool doButton(Context* c, float x, float y, float w, float h, const char* label, float originX = 0.5f, float originY = 0.5f)
{
	drawBox(c->NVG, x, y, w, h, srgb(100, 0, 0), originX, originY);

	float ascender, descender, height;
	nvgTextMetrics(c->NVG, &ascender, &descender, &height);

	float bounds[4];
	nvgTextBounds(c->NVG, 0, 0, label, nullptr, bounds);

	float textWidth = bounds[2] - bounds[0];
	float textHeight = bounds[3] - bounds[1];

	nvgFillColor(c->NVG, srgb(255, 255, 255));
	nvgText(c->NVG, x - textWidth * 0.5f, y - textHeight * originY + ascender * 0.5f, label, nullptr);

	if (c->MouseX >= x - w * originX && c->MouseX <= x + w * (1.0f - originX) &&
		c->MouseY >= y - h * originY && c->MouseY <= y + h * (1.0f - originY))
	{
		if (c->LButtonDown == false && c->LButtonTransitionCount > 0)
		{
			return true;
		}
	}

	return false;
}

bool doMessageBox(Context* c, const char* message)
{
	c->Modal = (void*)message;

	const float buttonHeight = 30.0f;
	const float buttonPadding = 15.0f;

	float ascender, descender, height;
	nvgTextMetrics(c->NVG, &ascender, &descender, &height);

	float maxTextboxWidth = g_screenWidth * 0.8f;
	float minWidth = 200.0f;

	float bounds[4];
	nvgTextBoxBounds(c->NVG, 0, 0, maxTextboxWidth, message, nullptr, bounds);

	float screenCenterX = (float)(g_screenWidth / 2);
	float screenCenterY = (float)(g_screenHeight / 2);

	float textWidth = bounds[2] - bounds[0];
	float textHeight = bounds[3] - bounds[1];

	const float padding = 5.0f;
	float boxWidth = textWidth + padding * 2.0f;
	float boxHeight = textHeight + padding * 2.0f + buttonHeight + buttonPadding;
	if (boxWidth < minWidth) boxWidth = minWidth;
	
	drawBox(c->NVG, screenCenterX, screenCenterY, boxWidth, boxHeight, srgb(100, 100, 100));

	nvgFillColor(c->NVG, nvgRGB(255, 255, 255));
	nvgTextBox(c->NVG, screenCenterX - textWidth / 2, screenCenterY - boxHeight * 0.5f + padding + height, maxTextboxWidth, message, nullptr);

	if (doButton(c, screenCenterX, screenCenterY + boxHeight * 0.5f - padding, 100.0f, 30.0f, "Got it!", 0.5f, 1.0f))
	{
		c->Modal = nullptr;
		return true;
	}

	return false;
}

void drawFileMenu(Context* c, int font)
{
	auto clickedItem = doMenuBar(c, font, menu, 3);

	if (clickedItem != nullptr)
		menuClicked(c, clickedItem);
}

void refresh(SDL_Window* window, Context* c, ListBoxData* data, int font)
{
	glViewport(0, 0, g_screenWidth, g_screenHeight);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	nvgBeginFrame(c->NVG, g_screenWidth, g_screenHeight, 1.0f);

	doListbox(c, data, 0, 30, (float)g_screenWidth, (float)(g_screenHeight - 30));
	drawFileMenu(c, font);

	if (c->MsgBoxMessage)
		if (doMessageBox(c, c->MsgBoxMessage))
		{
			c->MsgBoxMessage = nullptr;
			c->Invalidated = true;
		}

	nvgEndFrame(c->NVG);

	SDL_GL_SwapWindow(window);
}


int main(int argc, char* argv[])
{
	SDL_Init(SDL_INIT_VIDEO);

	auto window = SDL_CreateWindow("EGG archive browser", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, g_screenWidth, g_screenHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (window == nullptr)
		return -1;

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);

	auto gl = SDL_GL_CreateContext(window);
	if (gl == nullptr)
		return -1;

	if (glewInit() != GLEW_OK)
		return -1;

#ifdef ENABLE_SRGB
	glEnable(GL_FRAMEBUFFER_SRGB);
#endif

	NVGcontext* vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);

	int font = nvgCreateFont(vg, "Roboto", "Roboto-regular.ttf");

	Context c = {};
	c.NVG = vg;
	
	const char* headerNames[] = {
		"File name",
		"Compressed size",
		"Type",
		"Compression"
	};
	int columnWidths[] = {
		300, 150, 100, 50
	};
	data.HeaderNames = headerNames;
	data.ColumnWidths = columnWidths;
	data.NumColumns = 4;
	data.FirstVisibleRow = 0;
	data.NumRows = 0;

	while (!g_quitRequested)
	{
		c.Invalidated = false;
		c.LButtonTransitionCount = 0;
		c.MouseWheel = 0;

		bool awake = false;
		SDL_Event e;
		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
			case SDL_QUIT:
				g_quitRequested = true;
				awake = true;
				break;
			case SDL_MOUSEMOTION:
				c.MouseX = e.motion.x;
				c.MouseY = e.motion.y;
				awake = true;
				break;
			case SDL_MOUSEWHEEL:
				c.MouseWheel = e.wheel.y;
				awake = true;
				break;
			case SDL_MOUSEBUTTONUP:
				c.LButtonDown = false;
				c.LButtonTransitionCount++;
				awake = true;
				SDL_CaptureMouse(SDL_FALSE);
				break;
			case SDL_MOUSEBUTTONDOWN:
				c.LButtonDown = true;
				c.LButtonTransitionCount++;
				awake = true;
				SDL_CaptureMouse(SDL_TRUE);
				break;
			case SDL_WINDOWEVENT:
				switch (e.window.event)
				{
				case SDL_WINDOWEVENT_RESIZED:
				case SDL_WINDOWEVENT_SIZE_CHANGED:
					g_screenWidth = e.window.data1;
					g_screenHeight = e.window.data2;
				}
				break;
			}
		}

		refresh(window, &c, &data, font);

		if (g_quitRequested)
			break;

		if (c.Invalidated)
			continue;

		SDL_WaitEventTimeout(nullptr, 1000);
	}

	return 0;
}

#define MONDEGREENGAMES_EGG_IMPLEMENTATION
#include "egg.h"