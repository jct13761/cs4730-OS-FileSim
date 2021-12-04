#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs.h"
#include "fs_util.h"
#include "disk.h"

char inodeMap[MAX_INODE / 8];
char blockMap[MAX_BLOCK / 8];
Inode inode[MAX_INODE];
SuperBlock superBlock;
Dentry curDir;
int curDirBlock;

int fs_mount(char *name) {
	int numInodeBlock = (sizeof(Inode) * MAX_INODE) / BLOCK_SIZE;
	int i, index, inode_index = 0;

	// load superblock, inodeMap, blockMap and inodes into the memory
	if (disk_mount(name) == 1)
	{
		disk_read(0, (char *)&superBlock);
		if (superBlock.magicNumber != MAGIC_NUMBER)
		{
			printf("Invalid disk!\n");
			exit(0);
		}
		disk_read(1, inodeMap);
		disk_read(2, blockMap);
		for (i = 0; i < numInodeBlock; i++)
		{
			index = i + 3;
			disk_read(index, (char *)(inode + inode_index));
			inode_index += (BLOCK_SIZE / sizeof(Inode));
		}
		// root directory
		curDirBlock = inode[0].directBlock[0];
		disk_read(curDirBlock, (char *)&curDir);
	}
	else
	{
		// Init file system superblock, inodeMap and blockMap
		superBlock.magicNumber = MAGIC_NUMBER;
		superBlock.freeBlockCount = MAX_BLOCK - (1 + 1 + 1 + numInodeBlock);
		superBlock.freeInodeCount = MAX_INODE;

		//Init inodeMap
		for (i = 0; i < MAX_INODE / 8; i++)
		{
			set_bit(inodeMap, i, 0);
		}
		//Init blockMap
		for (i = 0; i < MAX_BLOCK / 8; i++)
		{
			if (i < (1 + 1 + 1 + numInodeBlock))
				set_bit(blockMap, i, 1);
			else
				set_bit(blockMap, i, 0);
		}
		//Init root dir
		int rootInode = get_free_inode();
		curDirBlock = get_free_block();

		inode[rootInode].type = directory;
		inode[rootInode].owner = 0;
		inode[rootInode].group = 0;
		gettimeofday(&(inode[rootInode].created), NULL);
		gettimeofday(&(inode[rootInode].lastAccess), NULL);
		inode[rootInode].size = 1;
		inode[rootInode].blockCount = 1;
		inode[rootInode].directBlock[0] = curDirBlock;

		curDir.numEntry = 1;
		strncpy(curDir.dentry[0].name, ".", 1);
		curDir.dentry[0].name[1] = '\0';
		curDir.dentry[0].inode = rootInode;
		disk_write(curDirBlock, (char *)&curDir);
	}
	return 0;
} // fs_mount()

int fs_umount(char *name) {
	int numInodeBlock = (sizeof(Inode) * MAX_INODE) / BLOCK_SIZE;
	int i, index, inode_index = 0;
	disk_write(0, (char *)&superBlock);
	disk_write(1, inodeMap);
	disk_write(2, blockMap);
	for (i = 0; i < numInodeBlock; i++)
	{
		index = i + 3;
		disk_write(index, (char *)(inode + inode_index));
		inode_index += (BLOCK_SIZE / sizeof(Inode));
	}
	// current directory
	disk_write(curDirBlock, (char *)&curDir);

	disk_umount(name);
} // fs_umount()

int search_cur_dir(char *name) {
	// return inode. If not exist, return -1
	int i;

	for (i = 0; i < curDir.numEntry; i++)
	{
		if (command(name, curDir.dentry[i].name))
			return curDir.dentry[i].inode;
	}
	return -1;
} // search_cur_dir()

void remove_from_dir(char *name) {
	int i;

	// find the directory that will be removed 
	for (i = 0; i < curDir.numEntry; i++) {
		if (command(name, curDir.dentry[i].name)) {
			break;
		} // if
	} // for

	// startign at the index of the file to be deleted, copy the items at the index over 
	for (int j = i+1; j < curDir.numEntry; j++) {
		// strncpy(curDir.dentry[i].name, curDir.dentry[j].name, strlen(curDir.dentry[j].name));
		// curDir.dentry[j].name = curDir.dentry[i].name;
		curDir.dentry[i] = curDir.dentry[j];
		i++;
	} // for

	// decrement the number of directory entries.
	curDir.numEntry--;
} // remove_from_dir()

// Create a file 
int file_create(char *name, int size) {
	int i;

	if (size > SMALL_FILE)
	{
		printf("Do not support files larger than %d bytes.\n", SMALL_FILE);
		return -1;
	}

	if (size < 0)
	{
		printf("File create failed: cannot have negative size\n");
		return -1;
	}

	int inodeNum = search_cur_dir(name);
	if (inodeNum >= 0)
	{
		printf("File create failed:  %s exist.\n", name);
		return -1;
	}

	if (curDir.numEntry + 1 > MAX_DIR_ENTRY)
	{
		printf("File create failed: directory is full!\n");
		return -1;
	}

	int numBlock = size / BLOCK_SIZE;
	if (size % BLOCK_SIZE > 0)
		numBlock++;

	if (numBlock > superBlock.freeBlockCount)
	{
		printf("File create failed: data block is full!\n");
		return -1;
	}

	if (superBlock.freeInodeCount < 1)
	{
		printf("File create failed: inode is full!\n");
		return -1;
	}

	char *tmp = (char *)malloc(sizeof(int) * size + 1);

	rand_string(tmp, size);
	printf("New File: %s\n", tmp);

	// get inode and fill it
	inodeNum = get_free_inode();
	if (inodeNum < 0)
	{
		printf("File_create error: not enough inode.\n");
		return -1;
	}

	inode[inodeNum].type = file;
	inode[inodeNum].owner = 1; // pre-defined
	inode[inodeNum].group = 2; // pre-defined
	gettimeofday(&(inode[inodeNum].created), NULL);
	gettimeofday(&(inode[inodeNum].lastAccess), NULL);
	inode[inodeNum].size = size;
	inode[inodeNum].blockCount = numBlock;
	inode[inodeNum].link_count = 1;

	// add a new file into the current directory entry
	strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
	curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
	curDir.dentry[curDir.numEntry].inode = inodeNum;
	printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, name);
	curDir.numEntry++;

	// get data blocks
	for (i = 0; i < numBlock; i++)
	{
		int block = get_free_block();
		if (block == -1)
		{
			printf("File_create error: get_free_block failed\n");
			return -1;
		}
		//set direct block
		inode[inodeNum].directBlock[i] = block;

		disk_write(block, tmp + (i * BLOCK_SIZE));
	}

	//update last access of current directory
	gettimeofday(&(inode[curDir.dentry[0].inode].lastAccess), NULL);

	printf("file created: %s, inode %d, size %d\n", name, inodeNum, size);

	free(tmp);
	return 0;
} // file_create()

int file_cat(char *name) {
	int inodeNum, i, size;
	char str_buffer[512];
	char *str;

	//get inode
	inodeNum = search_cur_dir(name);
	size = inode[inodeNum].size;

	//check if valid input
	if (inodeNum < 0) {
		printf("cat error: file not found\n");
		return -1;
	}
	if (inode[inodeNum].type == directory) {
		printf("cat error: cannot read directory\n");
		return -1;
	}

	//allocate str
	str = (char *)malloc(sizeof(char) * (size + 1));
	str[size] = '\0';

	for (i = 0; i < inode[inodeNum].blockCount; i++) {
		int block;
		block = inode[inodeNum].directBlock[i];

		disk_read(block, str_buffer);

		if (size >= BLOCK_SIZE) {
			memcpy(str + i * BLOCK_SIZE, str_buffer, BLOCK_SIZE);
			size -= BLOCK_SIZE;
		} else {
			memcpy(str + i * BLOCK_SIZE, str_buffer, size);
		}
	}
	printf("%s\n", str);

	//update lastAccess
	gettimeofday(&(inode[inodeNum].lastAccess), NULL);

	free(str);

	//return success
	return 0;
} // file_cat()

/**************************************************************************************************
* This function will read the specified file name's content starting at the offset and reading 
* the size amount. 
**************************************************************************************************/
int file_read(char *name, int offset, int size) {

	/*
	* PSEUDOCODE STEPS:
	* 1) Read i-node
	* 2) Read data
	* 3) Write i-node (time of access)
	*/

	// function variables 
	int i;
	char str_buffer[512];
	char *str;

	// get the i-node number of the file 
	int inodeNum = search_cur_dir(name);
	// if the i node is valie (the file exists)
	if (inodeNum < 0) {
		printf("File read failed: %s does not exist.\n", name);
		return -1;
	} // if 

	// if the file is a directory 
	if (inode[inodeNum].type == directory) {
		printf("File read failed: %s is a directory.\n", name);
		return -1;
	} // if 

	// if the size is invalid 
	if (inode[inodeNum].size < size) {
		printf("File read failed: %s size of read request is too large .\n", name);
		return -1;
	} // if

	// allocate str to read the disk content into.
	str = (char *)malloc(sizeof(char) * (size + 1));
	str[size] = '\0';
	int block_count = inode[inodeNum].blockCount;

	// loop over all the blocks in the file and read the contents 
	for (i = 0; i < block_count; i++) {
		// get the current block at i
		int block = inode[inodeNum].directBlock[i];
		// read the contents from the disk into the buffer.
		disk_read(block, str_buffer);
		// if the size is larger than the block size, 
		// copy the string from the memory location
		// ... else just copy from the current block's memory location
		if (size >= BLOCK_SIZE) {
			memcpy(str + i * BLOCK_SIZE, str_buffer + offset, BLOCK_SIZE);
			size -= BLOCK_SIZE;
		} else {
			memcpy(str + i * BLOCK_SIZE, str_buffer + offset, size);
		} // if 
	} // for 

	// print the contents of the file 
	printf("%s\n", str);

	//update lastAccess
	gettimeofday(&(inode[inodeNum].lastAccess), NULL);

	// deallocate the str buffer 
	free(str);

	//return success
	return 0;
	
} // file_read

int file_stat(char *name) {
	char timebuf[28];
	int inodeNum = search_cur_dir(name);
	if (inodeNum < 0) {
		printf("file cat error: file does not exist.\n");
		return -1;
	} // if

	printf("Inode\t\t= %d\n", inodeNum);
	if (inode[inodeNum].type == file)
		printf("type\t\t= File\n");
	else
		printf("type\t\t= Directory\n");
	printf("owner\t\t= %d\n", inode[inodeNum].owner);
	printf("group\t\t= %d\n", inode[inodeNum].group);
	printf("size\t\t= %d\n", inode[inodeNum].size);
	printf("link_count\t= %d\n", inode[inodeNum].link_count);
	printf("num of block\t= %d\n", inode[inodeNum].blockCount);
	format_timeval(&(inode[inodeNum].created), timebuf, 28);
	printf("Created time\t= %s\n", timebuf);
	format_timeval(&(inode[inodeNum].lastAccess), timebuf, 28);
	printf("Last acc. time\t= %s\n", timebuf);
} // file_stat()

/**************************************************************************************************
* NEED TO IMPLEMENT!!
* This function will remove the file by name.
**************************************************************************************************/
int file_remove(char *name) {
	/* 
	 * PSUEDOCODE STEPS:
	 * 1) Check that file exists
	 * 2) Check if link count == 1
	 * 		2.a) just delete the file 
	 * 3) if link_count > 1
	 * 		3.a) just remove the fine from the directory
	 * 		3.b) reduce the link count of the i-node
	*/

	// get the i-node number of the file 
	int inodeNum = search_cur_dir(name);
	// if the i node is valie (the file exists)
	if (inodeNum < 0) {
		printf("File removal failed: %s does not exist.\n", name);
		return -1;
	} // if 

	// if the file is a directory 
	if (inode[inodeNum].type == directory) {
		printf("File removal failed: %s is a directory.\n", name);
		return -1;
	} // if 

	// if the file has no links
	// else if the file has one or more links 
	if (inode[inodeNum].link_count == 1) {
		// File has no links

		// remove from directory
		remove_from_dir(name); 

		// clear up inode bitmap
		set_free_inode(inodeNum); 

		// clear up data block bitmap
		for (int i = 0; i < inode[inodeNum].blockCount; i++) {
			set_free_block(inode[inodeNum].directBlock[i]); // need to loop this probably
		} // for

		printf("%s has been successfully removed\n", name); 

		return 0;
	} else if (inode[inodeNum].link_count > 1) {
		// File has at least one link 

		// remove from directory
		remove_from_dir(name); 

		// decrease the link count
		inode[inodeNum].link_count--;

		printf("%s has been successfully removed\n", name); 
		return 0;
	} // if-else 

	// will only happen if something went wrong
	printf("Error! %s has not been removed\n", name); 
	return -1;
}

/**************************************************************************************************
* BONUS POINTS 
* Create a directory 
**************************************************************************************************/
int dir_make(char *name) {

	// check if the name is already in the directory
	int inodeNum = search_cur_dir(name);
	if (inodeNum >= 0) {
		printf("Directory make failed: \'/%s\' exist.\n", name);
		return -1;
	} // if 

	// check if there is space in the directory
	if (curDir.numEntry + 1 > MAX_DIR_ENTRY) {
		printf("Directory make failed: directory is full!\n");
		return -1;
	} // if 

	// check if the inode count is full
	if (superBlock.freeInodeCount < 1) {
		printf("Directory make failed: inode is full!\n");
		return -1;
	} // if 

	// check if the block count is full 
	int numBlock = 1;
	if (numBlock > superBlock.freeBlockCount) {
		printf("Directory make failed: data block is full!\n");
		return -1;
	} // if

	// get a free inode
	int dirInode = get_free_inode();
	if (dirInode < 0) {
		printf("Directory make error: not enough inode.\n");
		return -1;
	} // if 

	// Init root dir
	int dirBlock = get_free_block();
	if (dirBlock < 0) {
		printf("Directory make error: not enough free blocks.\n");
		return -1;
	} // if

	// Set the inode info for the new directory
	inode[dirInode].type = directory;
	inode[dirInode].owner = 1;
	inode[dirInode].group = 2;
	gettimeofday(&(inode[dirInode].created), NULL);
	gettimeofday(&(inode[dirInode].lastAccess), NULL);
	inode[dirInode].size = 1;
	inode[dirInode].blockCount = 1;
	inode[dirInode].directBlock[0] = dirBlock;

	// add a new directory into the current directory entry
	strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
	curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
	curDir.dentry[curDir.numEntry].inode = dirInode;
	// printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, name);
	curDir.numEntry++;

	Dentry newDir;	// create a new direcotry entry 

	// create the current dir "." entry and store the relevant info
	newDir.numEntry = 2;
	strncpy(newDir.dentry[0].name, ".", 1);
	newDir.dentry[0].name[1] = '\0';
	newDir.dentry[0].inode = dirInode;
	// disk_write(dirBlock, (char *)&newDir);

	// create the parent dir ".." entry and store the relevant info
	strncpy(newDir.dentry[1].name, "..", 2);
	newDir.dentry[1].name[3] = '\0';
	newDir.dentry[1].inode = curDir.dentry[0].inode;
	// disk_write(dirBlock, (char *)&curDir);

	// write the new Direcotry entry struct it to the new block
	disk_write(dirBlock, (char *)&newDir);

	// Print a message to the user that the creation was a success 	
	printf("Directory \'/%s\' created successfully\n", name);

	return 0;
} // dir_make()

/**************************************************************************************************
* BONUS POINTS 
* Remove a directory 
**************************************************************************************************/
int dir_remove(char *name) {

	/* 
	 * PSUEDOCODE STEPS:
	 * 1) Check that dircetory exists and is not a file 
	 * 2) if there are directories inside dir, make recursive call to dir_remove()
	 * 		2.a) remove from the current directory list
	 * 		2.b) Free i-node entry
	 * 		2.c) Free data blocks
	 * 3) If there are Files in the directory, remove them all
	 * 
	*/

	// check if the name is already in the directory
	int inodeNum = search_cur_dir(name);
	if (inodeNum < 0) {
		printf("Directory remove failed: \'/%s\' does not exist.\n", name);
		return -1;
	} // if 

	// if the dir is a file 
	if (inode[inodeNum].type == file) {
		printf("Directory removal failed: \'/%s\' is a file.\n", name);
		return -1;
	} // if 

	Dentry dirToBeDeleted; // the reference for the file structure to be deleted 

	// get the block number of the directory to switch to 
	int dirBlocktoBeDeleted = inode[inodeNum].directBlock[0];
	// load the dest dir's entry table into memory
	disk_read(dirBlocktoBeDeleted, (char *)&dirToBeDeleted);

	printf("dir To be deleted count = %d\n", dirToBeDeleted.numEntry); // TEST

	// if trying to delete the current directory 
	if (dirToBeDeleted.dentry[0].inode == curDir.dentry[0].inode) {
		printf("Directory removal failed: \'/%s\' is the current Directory.\n", name);
		return -1;
	} // if 

	// if trying to delete a parent directory
	if (dirToBeDeleted.dentry[0].inode == curDir.dentry[1].inode) { 
		printf("Directory removal failed: \'/%s\' is the current Directory's parent.\n", name);
		return -1;
	} // if

	// if the Directory is not empty 
	if (dirToBeDeleted.numEntry > 2) {
		printf("Failed to remove \'/%s\': Directory not empty!\n", name);
		return -1;
	} // if

	// printf("Directory is empty!\n"); // TEST

	// remove from directory
	remove_from_dir(name); 

	// clear up inode bitmap
	set_free_inode(inodeNum); 

	// clear up data block bitmap
	for (int i = 0; i < inode[inodeNum].blockCount; i++) {
		set_free_block(inode[inodeNum].directBlock[i]); // need to loop this probably
	} // for


	printf("%s has been successfully removed\n", name); 
	return 0;
} // dir_remove()

/**************************************************************************************************
* BONUS POINTS 
* Change direcotries
**************************************************************************************************/
int dir_change(char *name) {
	// check if the name is already in the directory
	int inodeNum = search_cur_dir(name);
	if (inodeNum < 0) {
		printf("cd failed: \'/%s\' does not exist.\n", name);
		return -1;
	} // if 

	// check if the type is a file 
	if (inode[inodeNum].type == file) {
		printf("cd error: \'%s\' is a file\n", name);
		return -1;
	} // if

	// write current Directory changes to block before switching 
	disk_write(curDirBlock, (char *)&curDir);

	// get the block number of the directory to switch to 
	curDirBlock = inode[inodeNum].directBlock[0];
	// load the dest dir's entry table into memory
	disk_read(curDirBlock, (char *)&curDir);

	// Print a message to the user that the change was a success 	
	printf("Current directory: \'/%s\'\n", name); 

	return 0;
} // dir_change()

int ls()
{
	int i;
	for (i = 0; i < curDir.numEntry; i++)
	{
		int n = curDir.dentry[i].inode;
		if (inode[n].type == file)
			printf("type: file, ");
		else
			printf("type: dir, ");
		printf("name \"%s\", inode %d, size %d byte\n", curDir.dentry[i].name, curDir.dentry[i].inode, inode[n].size);
	}

	return 0;
} // ls()

int fs_stat()
{
	printf("File System Status: \n");
	printf("# of free blocks: %d (%d bytes), # of free inodes: %d\n", superBlock.freeBlockCount, superBlock.freeBlockCount * 512, superBlock.freeInodeCount);
} // fs_stat()

/**************************************************************************************************
* NEED TO IMPLEMENT!!
* This function will make a hard link of another file.
**************************************************************************************************/
int hard_link(char *src, char *dest) {
	/* 
	 * PSUEDOCODE STEPS: 
	 * 1) Check if src exists and is not directory 
	 * 2) if dest file does not exist 
	 * 3) create the new dest file 
	 * 4) make dest point to data 
	 * 5) increase link counter 
	*/ 

	// get the i-node number of the src file 
	int srcInodeNum = search_cur_dir(src);
	// if the i node is valid (the file exists)
	if (srcInodeNum < 0) {
		printf("Hard Link failed: %s does not exist.\n", src);
		return -1;
	} // if 

	// if the src file is a directory 
	if (inode[srcInodeNum].type == directory) {
		printf("Hard Link failed: %s is a directory.\n", src);
		return -1;
	} // if 

	// get the i-node of the dest file 
	int destInodeNum = search_cur_dir(dest);
	// if the desst file is valied (it exists)
	if (destInodeNum >= 0) {
		printf("Hard Link failed:  %s exist.\n", dest);
		return -1;
	} // if

	// if the directory is full, do not add the new file 
	if (curDir.numEntry + 1 > MAX_DIR_ENTRY) {
		printf("Hard Link failed: directory is full!\n");
		return -1;
	} // if 

	int srcSize = inode[srcInodeNum].size; // the size of the src file
	int srcNumBlock = inode[srcInodeNum].blockCount; // the number of blocks of the src file
	// if the the size does not fit in one block, increase the block count. 
	if (srcSize % BLOCK_SIZE > 0)
		srcNumBlock++;

	// if the number of blokcs of the src 
	if (srcNumBlock > superBlock.freeBlockCount) {
		printf("Hard Link failed: data block is full!\n");
		return -1;
	} // if 

	if (superBlock.freeInodeCount < 1) {
		printf("Hard Link failed: inode is full!\n");
		return -1;
	} //if 

	// update the last access time of the inode that now refers to both dest and src files 
	gettimeofday(&(inode[srcInodeNum].lastAccess), NULL);

	// update the link count of the src file 
	inode[srcInodeNum].link_count++;

	// add a new file into the current directory entry
	strncpy(curDir.dentry[curDir.numEntry].name, dest, strlen(dest));
	curDir.dentry[curDir.numEntry].name[strlen(dest)] = '\0';
	curDir.dentry[curDir.numEntry].inode = srcInodeNum;
	// printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, dest);
	curDir.numEntry++;

	//update last access of current directory
	gettimeofday(&(inode[curDir.dentry[0].inode].lastAccess), NULL);

	printf("link created: %s --> %d\n", dest, src);

	return 0;
} // hard_link()

int execute_command(char *comm, char *arg1, char *arg2, char *arg3, char *arg4, int numArg) {

	printf("\n");
	if (command(comm, "df"))
	{
		return fs_stat();

		// file command start
	}
	else if (command(comm, "create"))
	{
		if (numArg < 2)
		{
			printf("error: create <filename> <size>\n");
			return -1;
		}
		return file_create(arg1, atoi(arg2)); // (filename, size)
	}
	else if (command(comm, "stat"))
	{
		if (numArg < 1)
		{
			printf("error: stat <filename>\n");
			return -1;
		}
		return file_stat(arg1); //(filename)
	}
	else if (command(comm, "cat"))
	{
		if (numArg < 1)
		{
			printf("error: cat <filename>\n");
			return -1;
		}
		return file_cat(arg1); // file_cat(filename)
	}
	else if (command(comm, "read"))
	{
		if (numArg < 3)
		{
			printf("error: read <filename> <offset> <size>\n");
			return -1;
		}
		return file_read(arg1, atoi(arg2), atoi(arg3)); // file_read(filename, offset, size);
	}
	else if (command(comm, "rm"))
	{
		if (numArg < 1)
		{
			printf("error: rm <filename>\n");
			return -1;
		}
		return file_remove(arg1); //(filename)
	}
	else if (command(comm, "ln"))
	{
		return hard_link(arg1, arg2); // hard link. arg1: src file or dir, arg2: destination file or dir

		// directory command start
	}
	else if (command(comm, "ls"))
	{
		return ls();
	}
	else if (command(comm, "mkdir"))
	{
		if (numArg < 1)
		{
			printf("error: mkdir <dirname>\n");
			return -1;
		}
		return dir_make(arg1); // (dirname)
	}
	else if (command(comm, "rmdir"))
	{
		if (numArg < 1)
		{
			printf("error: rmdir <dirname>\n");
			return -1;
		}
		return dir_remove(arg1); // (dirname)
	}
	else if (command(comm, "cd"))
	{
		if (numArg < 1)
		{
			printf("error: cd <dirname>\n");
			return -1;
		}
		return dir_change(arg1); // (dirname)
	}
	else
	{
		fprintf(stderr, "%s: command not found.\n", comm);
		return -1;
	}
	return 0;
} // execute_command()