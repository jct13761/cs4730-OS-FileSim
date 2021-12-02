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

int fs_mount(char *name)
{
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
}

int fs_umount(char *name)
{
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
}

int search_cur_dir(char *name)
{
	// return inode. If not exist, return -1
	int i;

	for (i = 0; i < curDir.numEntry; i++)
	{
		if (command(name, curDir.dentry[i].name))
			return curDir.dentry[i].inode;
	}
	return -1;
}

int file_create(char *name, int size)
{
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
}

int file_cat(char *name)
{
	int inodeNum, i, size;
	char str_buffer[512];
	char *str;

	//get inode
	inodeNum = search_cur_dir(name);
	size = inode[inodeNum].size;

	//check if valid input
	if (inodeNum < 0)
	{
		printf("cat error: file not found\n");
		return -1;
	}
	if (inode[inodeNum].type == directory)
	{
		printf("cat error: cannot read directory\n");
		return -1;
	}

	//allocate str
	str = (char *)malloc(sizeof(char) * (size + 1));
	str[size] = '\0';

	for (i = 0; i < inode[inodeNum].blockCount; i++)
	{
		int block;
		block = inode[inodeNum].directBlock[i];

		disk_read(block, str_buffer);

		if (size >= BLOCK_SIZE)
		{
			memcpy(str + i * BLOCK_SIZE, str_buffer, BLOCK_SIZE);
			size -= BLOCK_SIZE;
		}
		else
		{
			memcpy(str + i * BLOCK_SIZE, str_buffer, size);
		}
	}
	printf("%s\n", str);

	//update lastAccess
	gettimeofday(&(inode[inodeNum].lastAccess), NULL);

	free(str);

	//return success
	return 0;
}

/**************************************************************************************************
* This function will read the specified file name's content starting at the offset and reading 
* the size amount. 
**************************************************************************************************/
int file_read(char *name, int offset, int size) {

	// function variables 
	int i;
	char str_buffer[512];
	char *str;

	/*
	* PSEUDOCODE STEPS:
	* 1) Read i-node
	* 2) Read data
	* 3) Write i-node (time of access)
	*/

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

int file_stat(char *name)
{
	char timebuf[28];
	int inodeNum = search_cur_dir(name);
	if (inodeNum < 0)
	{
		printf("file cat error: file is not exist.\n");
		return -1;
	}

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
}

/**************************************************************************************************
* NEED TO IMPLEMENT!!
* This function will remove the file by name.
**************************************************************************************************/
int file_remove(char *name) {
	printf("Error: rm is not implemented.\n");
	return 0;
}

/**************************************************************************************************
* BONUS POINTS 
**************************************************************************************************/
int dir_make(char *name) {
	printf("Error: mkdir is not implemented.\n");
	return 0;
}

/**************************************************************************************************
* BONUS POINTS 
**************************************************************************************************/
int dir_remove(char *name) {
	printf("Error: rmdir is not implemented.\n");
	return 0;
}

/**************************************************************************************************
* BONUS POINTS 
**************************************************************************************************/
int dir_change(char *name) {
	printf("Error: cd is not implemented.\n");
	return 0;
}

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
}

int fs_stat()
{
	printf("File System Status: \n");
	printf("# of free blocks: %d (%d bytes), # of free inodes: %d\n", superBlock.freeBlockCount, superBlock.freeBlockCount * 512, superBlock.freeInodeCount);
}

/**************************************************************************************************
* NEED TO IMPLEMENT!!
* This function will make a hard link of another file.
**************************************************************************************************/
int hard_link(char *src, char *dest) {
	printf("Error: ln is not implemented.\n");

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
		printf("File read failed: %s does not exist.\n", src);
		return -1;
	} // if 

	// if the src file is a directory 
	if (inode[srcInodeNum].type == directory) {
		printf("File read failed: %s is a directory.\n", src);
		return -1;
	} // if 

	// get the i-node of the dest file 
	int destInodeNum = search_cur_dir(dest);
	// if the desst file is valied (it exists)
	if (destInodeNum >= 0) {
		printf("File create failed:  %s exist.\n", dest);
		return -1;
	} // if

	// if the directory is full, do not add the new file 
	if (curDir.numEntry + 1 > MAX_DIR_ENTRY) {
		printf("File create failed: directory is full!\n");
		return -1;
	} // if 

	int i;
	int srcSize = inode[srcInodeNum].size; // the size of the src file
	int srcNumBlock = inode[srcInodeNum].blockCount; // the number of blocks of the src file
	// if the the size does not fit in one block, increase the block count. 
	if (srcSize % BLOCK_SIZE > 0)
		srcNumBlock++;

	// if the number of blokcs of the src 
	if (srcNumBlock > superBlock.freeBlockCount) {
		printf("File create failed: data block is full!\n");
		return -1;
	}

	if (superBlock.freeInodeCount < 1) {
		printf("File create failed: inode is full!\n");
		return -1;
	}

	// char *tmp = (char *)malloc(sizeof(int) * srcSize + 1);

	// rand_string(tmp, size);
	// printf("New File: %s\n", tmp);

	// // get inode and fill it
	// destInodeNum = get_free_inode();
	// if (destInodeNum < 0) {
	// 	printf("File_create error: not enough inode.\n");
	// 	return -1;
	// }

	// // set the i-node of the destination file 
	// inode[destInodeNum].type = file;
	// inode[destInodeNum].owner = 1; // pre-defined
	// inode[destInodeNum].group = 2; // pre-defined
	// gettimeofday(&(inode[destInodeNum].created), NULL);
	// gettimeofday(&(inode[destInodeNum].lastAccess), NULL);
	// inode[destInodeNum].size = srcSize;
	// inode[destInodeNum].blockCount = srcNumBlock;
	// inode[destInodeNum].link_count = 1;

	// update the last access time of the inode that now refers to both dest and src files 
	gettimeofday(&(inode[srcInodeNum].lastAccess), NULL);

	// update the link count of the src file 
	inode[srcInodeNum].link_count++;

	// add a new file into the current directory entry
	strncpy(curDir.dentry[curDir.numEntry].name, dest, strlen(dest));
	curDir.dentry[curDir.numEntry].name[strlen(dest)] = '\0';
	curDir.dentry[curDir.numEntry].inode = srcInodeNum;
	printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, dest);
	curDir.numEntry++;

	// // make the dest file have the same blocks in memory as the src file 
	// for (i = 0; i < srcNumBlock; i++) {		
	// 	inode[destInodeNum].directBlock[i] = inode[srcInodeNum].directBlock[i];
	// 	// printf("srcBlocks = %d, destBlocks = %d\n", inode[destInodeNum].directBlock[i], inode[srcInodeNum].directBlock[i]);
	// } // for

	//update last access of current directory
	gettimeofday(&(inode[curDir.dentry[0].inode].lastAccess), NULL);

	// printf("file created: %s, inode %d, size %d\n", name, inodeNum, size);

	return 0;
}

int execute_command(char *comm, char *arg1, char *arg2, char *arg3, char *arg4, int numArg)
{

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
}
