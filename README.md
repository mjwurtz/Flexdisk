# Flexdisk
Utilities for creating, verifying, reading and writing Flex disk images, including random files, and converting text and S19 files.
- *flan* is a FLex ANalyser that looks at all possible defects (at least, I hope so :-) )
- *flfmt* creates a Flex disk image (size and geometry are configurables);
- *fldump* extracts all files (with an option to include deleted files) in a directory whose name by default is the one of the disk image file;
- *flread* extracts only selected files to the current directory
- *flwrite*/*fldel* adds/deletes files to/from a disk image (including correct creation of saved random files). Overwriting existing files is not the default, but allowed;
- *flpunack* and *flpack* are for converting text file from/to compressed Flex format to/from unix text format (with tabs);
- *mot2cmd* converts an S19 file into a Flex .CMD file, including the launch address if it exists.  This command can then be copied to a disk image with _flwrite_.

## TODO
- Correct remaining bugs (don't hesitate to signal them...) 
