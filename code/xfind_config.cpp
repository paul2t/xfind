
#define CONFIG_FILE_NAME "xfind.ini"
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
		for (i32 linei = 0; linei < nbLines; ++linei)
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
			}
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
		}
	}
	return conf;
}

void writeConfig(Config conf)
{
	FILE* configFile = fopen(CONFIG_FILE_NAME, "w");
	if (configFile)
	{
		String* oconfig = &conf.path;
		fprintf(configFile, "window=%u %u %u\n", conf.width, conf.height, conf.maximized);
		fprintf(configFile, "font=%f %.*s\n", conf.fontSize, strexp(conf.fontFile));
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

		for (int ki = 0; ki < ArrayCount(configKeys); ++ki)
		{
			fprintf(configFile, "%.*s%.*s\n", strexp(configKeys[ki]), strexp(oconfig[ki]));
		}
		fclose(configFile);
	}
}

