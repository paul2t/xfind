xfind is a tool to search for text in multiple files inside several folders.  
  
First choose a directory and some file extensions.  
When indexing is finished (about 10 seconds for 30.000 files on my SSD)  
You can then quickly search for a string in those files.  
First it shows the file names matching your string.  
It then (almost) instantly shows the matching lines.  

![Example search](https://bitbucket.org/Rednaj/xfind/downloads/xfind_sample_4coder.png)

You can select a line with up/down arrows. Also page up/down.  
Then press enter to open this file using a specified command line.  
In the command line, %p will be replaced by the path of the file. %l will be replaced by the line number. %c will be replaced by the column number.  
You can specify as many arguments as you whish. There are some examples in the help command. Only Sublime Text 3 has been tested.  
When you close the program, the config you specified is saved in xfind.ini. It is restored when the program is launched.  

The maximum file size allowed is 10MB. This is too prevent loading giant files. If the file is bigger, only the first 10MB are loaded.  
The maximum number of results shown is 100. Could be easily increased, but would slow down the search.  

The indexing only consists of loading all the files into memory.  
The search is basic: it will look through all the files to look for the string.  

The indexing and search are multithreaded. It uses as many logical cores as your computer has.  
Therefore even while it is working (searching / indexing), the UI will be responsive.  
There is absolutely no optimization done yet.  

Only works on Windows (for now).  
Make sure you have the font next to the executable.  

Options:  
===
![options](https://bitbucket.org/Rednaj/xfind/downloads/xfind_options.png)

Change the font size.  
The font can only be changed in the xfind.ini file. Which is generated after launching the program for the first time.  
Show/Hide the program's command input. (to have more vertical space)  
Show/Hide the inputs for folders and extensions.  
Show relative/full paths  
Search hidden files/folders  
Search/Ignore the file names  
