/*Copyright (C) 2014 Justin Green

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#include "vp.h"

struct file_node file_tree_root;
struct file_node last_file_created;
struct file_node first_dir_entry = {.offset = sizeof (struct vp_header), .size = 0, .name = NULL, .num_children = 0, .children = NULL, .data = NULL};
int num_direntries = 1;

void read_dir (struct file_node* parent, DIR* to_read, char* path);
char* append_dir_path (char* string1, char* string2);
void write_vp (char* path);
void write_vp_header (FILE* to_write);
void write_files (FILE* to_write, struct file_node* parent);
void write_dir_info (FILE* to_write, struct file_node* parent);

//Takes a directory path and appends a subdirectory to it.
//For example, append_dir_path ("/var/cache/", "pacman") would yield "/var/cache/pacman/"
char* append_dir_path (char* string1, char* string2)
{
	size_t string1_length = strlen (string1);
	size_t string2_length = strlen (string2);
	size_t new_size = string1_length + string2_length + 2;
	char* ret;
	int loop = 0;

	//Allocate enough space for the buffer
	ret = malloc (new_size);
	bzero (ret, new_size);

	//Copy the first string into the new buffer
	for (loop; loop < string1_length; loop ++)
		ret [loop] = string1 [loop];

	//The first string will likely be NULL terminated, overwrite the NULL terminator with the start of the second string
	if (!ret [loop-1])
	{
		loop --;
		string1_length --;
	}

	//Copy the second string
	for (loop; loop < new_size-2; loop ++)
		ret [loop] = string2 [loop-string1_length];

	//Add the '/' and add a new NULL terminator (the '/' clobbers the original one)
	ret [loop] = '/';
	ret [loop+1] = '\0';

	//Return the buffer of the new path
	return ret;
}

void read_dir (struct file_node* parent, DIR* to_read, char* path)
{
	DIR* subdir;
	struct dirent *entry; //This is the UNIX dir entry structure, not the one we use in VP files
	char* subdir_name;
	struct file_node* new_child;
	size_t file_size;
	FILE* input_file;
	struct stat status;

	entry = readdir (to_read);
	while (entry)
	{
		if (strcmp (entry->d_name, ".") && strcmp (entry->d_name, ".."))
		{
			subdir_name = append_dir_path (path, entry->d_name);

			//Try to open the given path as a directory
			errno = 0;
			subdir = opendir (subdir_name);

			num_direntries ++;

			//Initialize the new child
			parent->num_children ++;
			if (parent->num_children == 1)
				parent->children = malloc (sizeof (struct file_node));
			else
				parent->children = realloc (parent->children, parent->num_children * sizeof (struct file_node)); //Do note there's a possibility of an address change (THIS MEANS last_file_created cannot be a pointer)
			new_child = &(parent->children [parent->num_children-1]);
			new_child->children = NULL;
			new_child->num_children = 0;
			new_child->name = malloc (strlen (entry->d_name)+1);
			strcpy (new_child->name, entry->d_name);
			new_child->name [strlen (new_child->name)] = '\0';

			//Check if what we tried to open really WAS a directory
			if (errno != ENOTDIR) //No error message, must have been a directory
			{
				read_dir (new_child, subdir, subdir_name);
				closedir (subdir);
				new_child->offset = 0;
				new_child->size = 0;
				new_child->last_modified = 0;
			}
			else //Not a directory, load it up as a file
			{
				subdir_name [strlen (subdir_name)-1] = '\0'; //Remove the '/', just replace it with a '\0' to get a FILE path

				//Find the VP file offset
				new_child->offset = last_file_created.offset + last_file_created.size;

				//Open the file and read it into memory
				input_file = fopen (subdir_name, "r");
				fseek (input_file, 0, SEEK_END);
				file_size = ftell (input_file);
				new_child->data = malloc (file_size);
				fseek (input_file, 0, SEEK_SET);
				fread (new_child->data, 1, file_size, input_file);
				fclose (input_file);

				//Write the file size to the data structure
				new_child->size = file_size;

				//Update the last_file_created variable
				last_file_created = *new_child;

				//Get last modified timestamp
				stat (subdir_name, &status);
				new_child->last_modified = status.st_mtime;
			}

			//Cleanup dynamically allocated paths
			free (subdir_name);
		}

		//Get the next direntry
		entry = readdir (to_read);
	}
}

void write_vp (char* path)
{
	FILE* write_file;
	struct dir_entry root_direntry;

	//Open file for writing
	write_file = fopen (path, "w");

	//Write VP header
	write_vp_header (write_file);

	//Write all file content to the VP file
	write_files (write_file, &file_tree_root);

	//Write the "data" direntry
	fseek (write_file, 0, SEEK_END);
	root_direntry.de_offset = 0;
	root_direntry.de_size = 0;
	bzero (root_direntry.de_name, 32);
	strcpy (root_direntry.de_name, file_tree_root.name);
	root_direntry.de_timestamp = file_tree_root.last_modified;
	fwrite (&root_direntry, sizeof (struct dir_entry), 1, write_file);

	//Write the direntries to the end of the VP file
	write_dir_info (write_file, &file_tree_root);

	fclose (write_file);
}

void write_vp_header (FILE* to_write)
{
	struct vp_header header;

	//Initialize VP header
	header.vp_header [0] = 'V';
	header.vp_header [1] = 'P';
	header.vp_header [2] = 'V';
	header.vp_header [3] = 'P';
	header.vp_version = 2;
	header.vp_diroffset = last_file_created.offset + last_file_created.size;
	header.vp_direntries = num_direntries;

	//Write the header
	fseek (to_write, 0, SEEK_SET);
	fwrite (&header, sizeof (struct vp_header), 1, to_write);
}

void write_files (FILE* to_write, struct file_node* parent)
{
	int loop = 0;

	for (loop; loop < parent->num_children; loop ++)
	{
		if (parent->children [loop].size)
		{
			//Write file data if it is indeed a file
			fseek (to_write, 0, SEEK_END);
			fwrite (parent->children [loop].data, 1, parent->children [loop].size, to_write);

			//Cleanup the file data buffer
			free (parent->children [loop].data);
		}
		else
			write_files (to_write, &(parent->children [loop])); //Go down a subdirectory and copy those files
	}
}

void write_dir_info (FILE* to_write, struct file_node* parent)
{
	struct dir_entry current_dir_entry;
	int loop = 0;

	for (loop; loop < parent->num_children; loop ++)
	{
		//Reset dir entry name so it will always have a NULL at the end of the new file name
		bzero (current_dir_entry.de_name, 32);

		//Initialize dir entry
		current_dir_entry.de_offset = parent->children [loop].offset;
		current_dir_entry.de_size = parent->children [loop].size;
		strcpy (current_dir_entry.de_name, parent->children [loop].name);
		free (parent->children [loop].name); //Free the name buffers since they aren't required anymore
		current_dir_entry.de_timestamp = parent->children [loop].last_modified;

		//Write dir entry
		fseek (to_write, 0, SEEK_END);
		fwrite (&current_dir_entry, sizeof (struct dir_entry), 1, to_write);

		//Check if a subdirectory. If yes, follow it
		if (!parent->children [loop].size)
			write_dir_info (to_write, &(parent->children [loop]));
	}

	//Write a backdir to the end of the directory listing
	fseek (to_write, 0, SEEK_END);
	current_dir_entry.de_offset = 0;
	current_dir_entry.de_size = 0;
	bzero (current_dir_entry.de_name, 32);
	strcpy (current_dir_entry.de_name, "..");
	current_dir_entry.de_timestamp = 0;
	fwrite (&current_dir_entry, sizeof (struct dir_entry), 1, to_write);
}


int main (int argc, char** argv)
{
	FILE* output_file;
	DIR* input_directory;

	if (argc != 3)
	{
		printf ("Invalid arguments.\nUsage: yavpp <path to new VP file> <toplevel directory of contents>\n");
		exit (-1);
	}

	//Initialize toplevel "data" folder
	file_tree_root.offset = 0;
	file_tree_root.size = 0;
	file_tree_root.name = malloc (5);
	strcpy (file_tree_root.name, "data");
	file_tree_root.num_children = 0;
	file_tree_root.children = NULL;
	file_tree_root.data = NULL;
	file_tree_root.last_modified = 0;

	//Initialize last file created pointer
	last_file_created = first_dir_entry;

	//Open user specified directory and start reading
	input_directory = opendir (argv [2]);
	if (input_directory <= 0)
	{
		printf ("%s: No such file or directory\n", argv [2]);
		exit (-1);
	}
	read_dir (&file_tree_root, input_directory, argv [2]);
	closedir (input_directory);

	//Write the file tree to the VP file
	write_vp (argv [1]);

	//Cleanup
	free (file_tree_root.name);
}
