
void execOpenFile(String program, String filename, i32 fileline, i32 column)
{
	char buff[4096];
	String call = make_fixed_width_string(buff);
	for (i32 ci = 0; ci < program.size; ++ci)
	{
		char c = program.str[ci];
		if (char_is_slash(c) && call.size > 0 && char_is_slash(call.str[call.size - 1])) continue;

		if (0 == strnicmp(program.str + ci, ARG_HOME, sizeof(ARG_HOME) - 1))
		{
			String tail = tailstr(call);
			DWORD envSize = GetEnvironmentVariableA("home", tail.str, tail.memory_size);
			call.size += envSize;
			ci += sizeof(ARG_HOME) - 1 - 1;
		}
		else if (0 == strncmp(program.str+ci, ARG_PATH, sizeof(ARG_PATH)-1))
		{
			append(&call, filename);
			ci += sizeof(ARG_PATH) - 1 - 1;
		}
		else if (0 == strncmp(program.str+ci, ARG_LINE, sizeof(ARG_LINE)-1))
		{
			append_int_to_str(&call, fileline);
			ci += sizeof(ARG_LINE) - 1 - 1;
		}
		else if (0 == strncmp(program.str+ci, ARG_COL, sizeof(ARG_COL)-1))
		{
			append_int_to_str(&call, column);
			ci += sizeof(ARG_COL) - 1 - 1;
		}
		else
		{
			append(&call, c);
		}
	}
	terminate_with_null(&call);
	execProgram(call.str);
}

internal void showInput(char* name, float maxWidth, float lablelWidth, const ImVec4& labelColor = *(ImVec4*)0)
{
	ImGui::SetCursorPosX(maxWidth + 10 - lablelWidth);
	if (&labelColor)
		ImGui::TextColored(labelColor, name);
	else
		ImGui::Text(name);
}
internal bool showInput(char* id, String& input, float width = -1, b32* setFocus = 0, const ImVec4& textColor = *(ImVec4*)0)
{
	ImGui::SameLine();
	if (setFocus && *setFocus)
	{
		ImGui::SetKeyboardFocusHere();
		*setFocus = false;
	}
	if (&textColor)
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	ImGui::PushItemWidth(width);
	bool modified = ImGui::InputText(id, input.str, input.memory_size);
	ImGui::PopItemWidth();
	if (&textColor)
		ImGui::PopStyleColor();
	if (modified) input.size = str_size(input.str);
	return modified;
}

internal bool showInput(char* name, char* id, String& input, float maxWidth, float lablelWidth)
{
	showInput(name, maxWidth, lablelWidth);
	return showInput(id, input);
}

internal void showHighlightedText(String text, i32 highlightedOffset, i32 highlightedLen, bool sameLine = false)
{
	String beforeMatch = substr(text, 0, highlightedOffset);
	String matchingText = substr(text, highlightedOffset, highlightedLen);
	String afterMatch = substr(text, highlightedOffset + highlightedLen);


	if (beforeMatch.size)
	{
		if (sameLine)
			ImGui::SameLine(0, 0);
		sameLine = true;
		ImGui::Text("%.*s", strexp(beforeMatch));
	}
	if (matchingText.size)
	{
		if (sameLine)
			ImGui::SameLine(0, 0);
		sameLine = true;
		ImGui::TextColored(matchingColor, "%.*s", strexp(matchingText));
	}
	if (afterMatch.size)
	{
		if (sameLine)
			ImGui::SameLine(0, 0);
		sameLine = true;
		ImGui::Text("%.*s", strexp(afterMatch));
	}
}



bool CubicUpdateFixedDuration1(float *P0, float *V0, float P1, float V1, float Duration, float dt)
{
	bool Result = false;

	if (dt > 0)
	{
		if (Duration < dt)
		{
			*P0 = P1 + (dt - Duration)*V1;
			*V0 = V1;
			Result = true;
		}
		else
		{
			float t = (dt / Duration);
			float u = (1.0f - t);

			float C0 = 1 * u*u*u;
			float C1 = 3 * u*u*t;
			float C2 = 3 * u*t*t;
			float C3 = 1 * t*t*t;

			float dC0 = -3 * u*u;
			float dC1 = -6 * u*t + 3 * u*u;
			float dC2 = 6 * u*t - 3 * t*t;
			float dC3 = 3 * t*t;

			float B0 = *P0;
			float B1 = *P0 + (Duration / 3.0f) * *V0;
			float B2 = P1 - (Duration / 3.0f) * V1;
			float B3 = P1;

			*P0 = C0 * B0 + C1 * B1 + C2 * B2 + C3 * B3;
			*V0 = (dC0*B0 + dC1 * B1 + dC2 * B2 + dC3 * B3) * (1.0f / Duration);
		}
	}

	return(Result);
}


float scroll_time = 0;
float scroll_dy;
float scroll_target = 0;

internal void showResults(State& state, Match* results, i32 resultsSize, i32 resultsSizeLimit, FileIndex* fileIndex, i32& selectedLine)
{
	if (resultsSize <= 0)
	{
		ImGui::TextColored(filenameColor, "--- No results to show ---");
		return;
	}

	b32 selectionChanged = false;
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow)))
	{
		selectedLine--;
		selectionChanged = true;
	}
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow)))
	{
		selectedLine++;
		selectionChanged = true;
	}
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
	{
		selectedLine += 10;
		selectionChanged = true;
	}
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
	{
		selectedLine -= 10;
		selectionChanged = true;
	}

	if (selectedLine >= resultsSize)
		selectedLine = resultsSize - 1;

	if (selectedLine < 0)
		selectedLine = 0;

	float dt = ImGui::GetIO().DeltaTime;
	if (scroll_time > 0)
	{
		float scroll = ImGui::GetScrollY();
		CubicUpdateFixedDuration1(&scroll, &scroll_dy, scroll_target, 0.0f, scroll_time, dt);
		ImGui::SetScrollY(scroll);

		scroll_time -= dt;
		if (scroll_time < 0)
		{
			ImGui::SetScrollY(scroll_target);
			scroll_time = 0;
		}
	}


	float h = ImGui::GetTextLineHeightWithSpacing();
	float s = h - ImGui::GetTextLineHeight();
	ImVec2 avail = ImGui::GetContentRegionAvail();

	ImVec2 mouse = ImGui::GetMousePos() - ImGui::GetCursorScreenPos();
	float hoverIndexF = mouse.y / h;
	int hoverIndex = -1;
	if (hoverIndexF >= 0)
		hoverIndex = (int)hoverIndexF;
	if (hoverIndex >= resultsSize)
		hoverIndex = -1;
	if (mouse.x > avail.x)
		hoverIndex = -1;

	if (!ImGui::IsWindowHovered())
		hoverIndex = -1;

	//ImGui::BeginTooltip();
	//ImGui::Text("%f / %f : %f %d", mouse.x, mouse.y, hoverIndexF, hoverIndex);
	//ImGui::Text("%f -> %f", selectedLine*h, (selectedLine + 1)*h);
	//ImGui::Text("%f %f", diffDown, diffUp);
	//ImGui::EndTooltip();


	if (hoverIndex >= 0)
	{
		if (ImGui::IsMouseClicked(0))
		{
			selectedLine = hoverIndex;
			state.setFocusToSearchInput = true;
		}
		if (ImGui::IsMouseDoubleClicked(0))
		{
			Match match = results[hoverIndex];
			execOpenFile(state.config.tool, match.file->path, match.lineIndex, match.offset_in_line + 1);
			//Sleep(10);
			//SetForegroundWindow(glfwGetWin32Window(window));
		}
	}

	for (i32 ri = 0; ri < resultsSize && ri < resultsSizeLimit; ++ri)
	{
		bool highlighted = (ri == selectedLine);
		Match match = results[ri];
		FileIndexEntry fileindex = *match.file;
		String filename = state.config.showRelativePaths ? fileindex.relpath : fileindex.path;

		float scrollMax = ImGui::GetScrollMaxY();
		float scroll = ImGui::GetScrollY();

		ImVec2 lineStart = ImGui::GetCursorScreenPos() + ImVec2(0, -s / 2);
		ImVec2 lineEnd = lineStart + ImVec2(avail.x, h);

		if (highlighted)
		{
			ImGui::GetWindowDrawList()->AddRectFilled(lineStart, lineEnd, highlightColor);

			if (selectionChanged)
			{
				int padding = 2;
				float diffUp = (selectedLine - padding)*h - scroll;
				float diffDown = (selectedLine + 1 + padding)*h - avail.y - scroll;
				if (diffDown > 0)
				{
					//ImGui::SetScrollY((selectedLine + 1 + padding)*h - avail.y);
					scroll_target = (selectedLine + 1 + padding)*h - avail.y;
					scroll_time = 0.1f;
				}
				if (diffUp < 0)
				{
					//ImGui::SetScrollY((selectedLine - padding)*h);
					scroll_target = (selectedLine - padding)*h;
					scroll_time = 0.1f;
				}
			}
		}
		else if (ri == hoverIndex)
		{
			ImGui::GetWindowDrawList()->AddRectFilled(lineStart, lineEnd, ImColor(.2f, .2f, .2f));
		}

		if (results[ri].lineIndex > 0)
		{
			ImGui::TextColored(filenameColor, "%.*s", strexp(filename));
			ImGui::SameLine(0, 0);
			ImGui::TextColored(filenameColor, "(%d:%d): ", match.lineIndex, match.offset_in_line);

			showHighlightedText(match.line, match.offset_in_line, match.matching_length, true);
		}
		else
		{
			showHighlightedText(filename, match.offset_in_line + (state.config.showRelativePaths ? 0 : (fileindex.path.size - fileindex.relpath.size)), match.matching_length);
		}
	}

	if (resultsSize >= resultsSizeLimit)
	{
		ImGui::TextColored(filenameColor, "--- There are too many results, only showing the first %d ---", resultsSize);
	}
	else
	{
		ImGui::TextColored(filenameColor, "--- %d results ---", resultsSize);
	}
}

