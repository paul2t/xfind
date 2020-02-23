
#define CONFIG_FILE_NAME "xfind.ini"
#define DEFAULT_FONT_NAME "liberation-mono.ttf"

Config readConfig(MemoryArena& arena)
{
	Config conf = {};
	conf.content = readEntireFile(arena, CONFIG_FILE_NAME);
	if (conf.content.size)
	{
		TempMemory _tmp(arena);
		i32 nbLines = 0;
		String* lines = getLines(arena, conf.content, nbLines, true, true, true);
		String* oconfig = &conf.path;
		i32 firstLineIndex = 0;
		if (nbLines > 0)
		{
			if (lines[0].str[0] == '[')
			{
				++firstLineIndex;
				char* end;
				conf.version = fastStringToU32(lines[0].str, end);
			}
		}

		for (i32 linei = firstLineIndex; linei < nbLines; ++linei)
		{
			String line = lines[linei];
			for (int ki = 0; ki < ArrayCount(configKeys); ++ki)
			{
				if (match_part(line, configKeys[ki]))
				{
					oconfig[ki] = substr(line, configKeys[ki].size);
					break;
				}
			}
			String key = make_lit_string("window=");
			if (match_part(line, key))
			{
				char* end;
				conf.width = fastStringToU32(line.str + key.size, end);
				++end;
				conf.height = fastStringToU32(end, end);
				++end;
				conf.maximized = fastStringToU32(end, end);
			}
			String keyf = make_lit_string("font=");
			if (match_part(line, keyf))
			{
				char* end;
				conf.fontSize = strtof(line.str + keyf.size, &end);
				++end;
				conf.fontFile = substr(line, (i32)(end - line.str));
				if (match(conf.fontFile, make_lit_string(DEFAULT_FONT_NAME)))
					conf.fontFile.size = 0;
			}
			String ctxLinesK = make_lit_string("context_lines=");
			if (match_part(line, ctxLinesK))
			{
				char* end;
				conf.contextLines = fastStringToU32(line.str + ctxLinesK.size, end);
			}
			if (match(line, make_lit_string("hide_context_lines")))
				conf.hideContextLines = true;
			if (match(line, make_lit_string("hide_program")))
				conf.showProgram = false;
			if (match(line, make_lit_string("hide_folder_and_ext")))
				conf.showFolderAndExt = false;
			if (match(line, make_lit_string("show_full_path")))
				conf.showRelativePaths = false;
			if (match(line, make_lit_string("show_hidden_files")))
				conf.showHiddenFiles = true;
			if (match(line, make_lit_string("no_file_name_search")))
				conf.searchFileNames = false;
			if (match(line, make_lit_string("case_sensitive")))
				conf.caseSensitive = true;
		}

		if (conf.version == 0)
		{
			for (i32 i = 0; i < conf.tool.size; ++i)
			{
				char c = conf.tool.str[i];
				char c2 = conf.tool.str[i + 1];
				if (c == '?' && (c2 == 'p' || c2 == 'l' || c2 == 'c'))
				{
					conf.tool.str[i] = argChar;
				}
			}
		}
	}
	return conf;
}

void writeConfig(Config conf)
{
	Config defaultConf = {};
	FILE* configFile = fopen(CONFIG_FILE_NAME, "w");
	if (configFile)
	{
		fprintf(configFile, "[%d] # version\n", CONFIG_LATEST_VERSION);
		String* oconfig = &conf.path;
		fprintf(configFile, "window=%u %u %u\n", conf.width, conf.height, conf.maximized);
		String fontFile = conf.fontFile;
		if (fontFile.size <= 0)
			fontFile = make_lit_string(DEFAULT_FONT_NAME);
		fprintf(configFile, "font=%f %.*s\n", conf.fontSize, strexp(fontFile));
		if (!conf.showProgram)
			fprintf(configFile, "hide_program\n");
		if (!conf.showFolderAndExt)
			fprintf(configFile, "hide_folder_and_ext\n");
		if (!conf.showRelativePaths)
			fprintf(configFile, "show_full_path\n");
		if (conf.showHiddenFiles)
			fprintf(configFile, "show_hidden_files\n");
		if (!conf.searchFileNames)
			fprintf(configFile, "no_file_name_search\n");
		if (conf.caseSensitive)
			fprintf(configFile, "case_sensitive\n");
		if (conf.contextLines != defaultConf.contextLines)
			fprintf(configFile, "context_lines=%d\n", conf.contextLines);
		if (conf.hideContextLines)
			fprintf(configFile, "hide_context_lines\n");


		for (int ki = 0; ki < ArrayCount(configKeys); ++ki)
		{
			fprintf(configFile, "%.*s%.*s\n", strexp(configKeys[ki]), strexp(oconfig[ki]));
		}
		fclose(configFile);
	}
}

