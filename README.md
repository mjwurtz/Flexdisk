# Flexdisk
Utilities for creating, verifying, reading and writing Flex disk images, including random files, and converting text and S19 files.
- *flan* is a FLex ANalyser that looks at all possible defects (at least, I hope so :-) )
- *fflex* creates a Flex disk image (size and geometry are configurables)
- *fldump* extracts all files (with an option to include deleted files) in a directory whose name by default is the one of the disk
- *flwrite*/*fldel* adds/deletes files to/from a disk image (including correct creation of saved random files).
- *flpunack* and flpack are for converting text file from/to compressed Flex format to/from unix text format (with tabs)
- *mot2cmd* converts an S19 file into a Flex .CMD file, including the launch address if it exists.  This command can then be copied to a disk image with _flwrite_
