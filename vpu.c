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

#include "vp.h"

#define MODE_DEFAULT 0
#define MODE_SINGLE 1
#define MODE_LIST 2

char mode = MODE_DEFAULT;
struct file_node file_tree_root;
char* file_buf;
size_t file_size;
int num_tabs;

struct file_node* add_child (struct file_node* parent, struct dir_entry* child_data);
int parse_dir (struct file_node* parent, struct dir_entry* dir);
void write_tree (char* path, struct file_node* root);
void write_dir (char* path, struct file_node* dir);
void write_file (char* path, struct file_node* file);
char* parse_path (char* path);
struct file_node* find_file_by_path (char* path);

struct file_node* add_child (struct file_node* parent, struct dir_entry* child_data)
{
	struct file_node* current_child; //Local pointer to the child we're initializing so we don't have to do too many dereferences

	//Memory management
	parent->num_children ++;
	if (parent->children-1)
		parent->children = realloc (parent->children, parent->num_children * sizeof (struct file_node));
	else
		parent->children = malloc (sizeof (struct file_node));

	//Copy data
	current_child = &(parent->children [parent->num_children-1]);
	current_child->offset = child_data->de_offset;
	current_child->size = child_data->de_size;
	current_child->name = child_data->de_name; //Do note this doesn't actually copy the name, just the pointer to the name string

	//Initialize child's children information
	current_child->num_children = 0;
	current_child->children = NULL;

	return current_child;
}

int parse_dir (struct file_node* parent, struct dir_entry* dir)
{
	struct dir_entry* index;
	int loop = 0;
	int num_entries = 0;
	int sub_entries;
	struct file_node* new_child;

	//Find index based on directory offset
	index = (struct dir_entry*)((char*)dir + sizeof (struct dir_entry));

	//All directories end with a backdir entry, except the one at the very of the file. Use this to know when to stop traversing the directory.
	while (strcmp (index [loop].de_name, "..") && &(index [loop]) < file_buf + file_size)
	{
		//Add child to file tree
		new_child = add_child (parent, &(index [loop]));

		//Check if child was a directory
		if (!new_child->size)
		{	
			sub_entries = parse_dir (new_child, &(index [loop])); //Child was a directory, start traversing it
			sub_entries += 2;
			index = (struct dir_entry*)((char*)&(index [loop]) + sub_entries*sizeof (struct dir_entry));
			loop = 0;
			num_entries += sub_entries;
		}
		else
		{
			loop ++;
			num_entries ++;
		}
	}
	return num_entries;
}

void write_file (char* path, struct file_node* file)
{
	if (mode != MODE_LIST)
	{
		size_t total_length;
		size_t name_length;
		size_t path_length;
		char* full_path; //Directory path + the name of the file
		int loop = 0;
		FILE* output;

		//Memory management with full path buffer
		path_length = strlen (path);
		name_length = strlen (file->name);
		total_length = path_length + name_length;
		full_path = malloc (total_length + 1);
		bzero (full_path, total_length + 1); //Just to be safe...cannot assume freshly allocated memory will be filled with 0s

		//Figure out the path with file name
		for (loop; loop < path_length; loop ++)
			full_path [loop] = path [loop];
		if (!full_path [loop-1]) //Usually paths will come NULL terminated
		{
			loop --; //Overwrite this NULL terminator with data to append
			path_length --;
		}
		for (loop; loop < total_length; loop ++) //Copy name into path
			full_path [loop] = file->name [loop-path_length];

		//Write data to file
		output = fopen (full_path, "w");
		fwrite (file_buf + file->offset, 1, file->size, output);

		//Cleanup
		fclose (output);
		free (full_path);
	}
	else
	{
		int loop;
		for (loop = 0; loop < num_tabs; loop ++)
			printf ("\t");
		printf ("%s\n", file->name);
	}
}

void write_dir (char* path, struct file_node* dir)
{
	if (mode != MODE_LIST)
	{
		size_t total_length;
		size_t name_length;
		size_t path_length;
		char* new_path; //Directory path + directory name
		int loop = 0;

		//Memory management with new path buffer;
		path_length = strlen (path);
		name_length = strlen (dir->name);
		total_length = path_length + name_length;
		new_path = malloc (total_length + 2); //Count the NULL terminator and the '/'
		bzero (new_path, total_length + 2); //Just to be safe...cannot assume freshly allocated memory will be filled with 0s

		//Figure out the the new path
		for (loop; loop < path_length; loop ++)
			new_path [loop] = path [loop];
		if (!new_path [loop-1]) //Usually paths will come NULL terminated
		{
			loop --; //Overwrite this NULL terminator with data to append
			path_length --;
		}
		for (loop; loop < total_length; loop ++) //Append new subdirectory
			new_path [loop] = dir->name [loop-path_length];
		loop = strlen (new_path);
		new_path [loop] = '/';
		new_path [loop+1] = '\0';

		mkdir (new_path, 0777); //Create directory

		//Take care of everything INSIDE the directory
		write_tree (new_path, dir);

		//Cleanup
		free (new_path);
	}
	else
	{
		int loop;
		for (loop = 0; loop < num_tabs; loop ++)
			printf ("\t");
		printf ("%s\n", dir->name);
		num_tabs ++;
		write_tree (path, dir);
		num_tabs --;
	}
	free (dir->children); //Write functions are recursive, so when write_tree returns we are GUARANTEED to have no unwritten data left in this directory	
}

void write_tree (char* path, struct file_node* root)
{
	int loop = 0;

	//Traverse all children and call the appropriate write function
	for (loop; loop < root->num_children; loop ++)
	{
		if (root->children [loop].size) //Directories have a size field of 0, files have a size field of some finite integer
			write_file (path, &(root->children [loop]));
		else
			write_dir (path, &(root->children [loop]));
	}
}

char* parse_path (char* path)
{
	int loop = 0;
	char* ret;

	while (path [loop] && path [loop] != '/')
		loop ++;

	ret = malloc (loop + 2); //Include the NULL terminator and the /, if there is one

	strncpy (ret, path, loop + 1); //Include the / because it indicates that we still have more directory tree to go down

	ret [loop + 1] = '\0';

	return ret;
}

struct file_node* find_file_by_path (char* path)
{
	struct file_node* current = &file_tree_root;
	char* current_dir_name;
	int loop;
	int position = 5;

	current_dir_name = parse_path (path + position); //Skip data directory

	//Stop when our current dir name doesn't end in a '/', because then we've reached the filename itself
	while (current_dir_name [strlen (current_dir_name)-1] == '/')
	{
		for (loop = 0; loop < current->num_children; loop ++)
		{
			if (!strncmp (current->children [loop].name, current_dir_name, strlen (current_dir_name)-1))
				break;
		}
		if (loop == current->num_children)
		{
			printf ("Path not found in given VP file\n");
			exit (-1);
		}

		//Update the current node
		current = &(current->children [loop]);

		//Get the next part of the path
		position += strlen (current_dir_name);
		free (current_dir_name);
		current_dir_name = parse_path (path + position);
	}

	//Check the last directory in the path for the filename
	for (loop = 0; loop < current->num_children; loop ++)
	{
		if (!strncmp (current->children [loop].name, current_dir_name, strlen (current_dir_name)-1))
			break;
	}
	if (loop == current->num_children)
	{
		printf ("Path not found in given VP file\n");
		exit (-1);
	}

	return &(current->children [loop]);
}

int main (int argc, char** argv)
{
	int path_arg = 1;
	int vp_file_arg = 2;
	FILE* input;

	//Argument sanity check
	if (argc < 3 || argc == 4 || argc > 5)
	{
		printf ("Invalid arguments.\nUsage: yavpu <extraction path> <to extract>\nyavpu -s <extraction path> <to extract> <specific file to extract>\nyavpu -l <to extract>\n");
		exit (-1);
	}

	if (!strcmp (argv [1], "-s"))
	{
		mode = MODE_SINGLE;
		path_arg ++;
		vp_file_arg ++;
	}
	else if (!strcmp (argv [1], "-l"))
		mode = MODE_LIST;

	//Open VP file
	input = fopen (argv [vp_file_arg], "r");
	if (input <= 0)
	{
		printf ("%s: No such file or directory\n", argv [vp_file_arg]);
		exit (-1);
	}

	//Determine the size of the file
	fseek (input, 0, SEEK_END);
	file_size = ftell (input);

	//Memory management for file buffer
	file_buf = malloc (file_size);

	//Read VP file into memory
	fseek (input, 0, SEEK_SET);
	fread (file_buf, 1, file_size, input);
	fclose (input); //Done with the file at this point

	//Parse VP header and initialize root of file tree
	if (strncmp (((struct vp_header*)file_buf)->vp_header, "VPVP", 4))
	{
		printf ("Not a VP file\n");
		exit (-1);
	}
	file_tree_root.offset = ((struct vp_header*)file_buf)->vp_diroffset;
	file_tree_root.size = 0;
	file_tree_root.num_children = 0;
	file_tree_root.children = NULL;
	file_tree_root.name = malloc (5);
	memcpy (file_tree_root.name, "data\0", 5); //The VP specs say there will ALWAYS be a toplevel directory called data

	//Start parsing the memory image of the VP file
	parse_dir (&file_tree_root, (struct dir_entry*)(file_buf + ((struct vp_header*)file_buf)->vp_diroffset));

	if (mode != MODE_SINGLE)
		write_dir (argv [path_arg], &file_tree_root); //Write the file tree
	else
	{
		struct file_node* target = find_file_by_path (argv [4]);
		write_file (argv [path_arg], target); //Write the single target file
	}

	//Cleanup
	free (file_buf);
	free (file_tree_root.name);
}
