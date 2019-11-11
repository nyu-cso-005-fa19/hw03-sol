#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "minheap.h"

// A node in a Huffman code tree
struct code_tree_s {
  char data;
  int weight;
  struct code_tree_s* left;
  struct code_tree_s* right;
};

typedef struct code_tree_s code_tree;

// A code table entry
struct code_s {
  unsigned char bits[16];
  int len;
};

typedef struct code_s code;

int weights[128] = {};

code ctable[128] = {};

FILE* open_file(char* file_name, char* mode) {
  FILE* file = fopen(file_name, mode);
  if (file == NULL) {
    fprintf(stderr, "Error: can't open file %s.\n", file_name);
    abort();
  }
  return file;
}

// size of buffers used for reading from files
#define SIZE 128

// Count the number of occurrences of each ASCII character in the
// file plain_file_name and store them in the array weights.
// The size of weights should be 128 (the number of ASCII characters).
void count_occurrences(char* plain_file_name, int* weights) {
  
  char buf[SIZE];

  FILE* file = open_file(plain_file_name, "r");
  
  while (fgets(buf, sizeof(buf), file)) {
    for (int i = 0; buf[i] != '\0'; ++i) {
      weights[buf[i]]++;
    }
  }

  fclose(file);  
}

// Write the weights out into the file weight_file_name.
// The size of weights should be 128 (the number of ASCII characters).
void write_weight_table(char* weight_file_name, int* weights) {
  FILE* file = open_file(weight_file_name, "w");

  for(int i = 0; i < 128; i++) {
    if (weights[i] > 0)
      fprintf(file, "%c:%d\n", i, weights[i]);
  }
  
  fclose(file);
}

// Read the contents of file weight_file_name and write the result into
// the array weights.
// The size of weights should be 128 (the number of ASCII characters).
void read_weight_table(char* weight_file_name, int* weights) {
  FILE* file = open_file(weight_file_name, "r");

  char read_buffer[SIZE];

  while (fgets(read_buffer, sizeof(read_buffer), file)) {
    char* p = read_buffer;

    while (*p != '\0') {
      char c = *p;

      if (*p == '\n') {
        fgets(read_buffer, sizeof(read_buffer), file);
        p = read_buffer;
      } else {
        p++;
      }
      
      if (*p != ':') {
        fprintf(stderr, "Invalid weight specification\n");
        fclose(file);
        abort();
      }
      
      p++;

      unsigned long l = strtoul(p, &p, 10);

      weights[c] = l;
      
      if (*p != '\n') {
        fprintf(stderr, "Invalid weight specification\n");
        fclose(file);
        abort();
      }

      p++;
    }
  }
    
  
  fclose(file);
}

// Return the weight of the tree rooted at n
int weight_of_tree(code_tree* n) {
  if (n == NULL) return 0;
  else return n->weight;
}

// Create a new leaf node of a code tree.
code_tree* make_leaf(char c, int w) {
  code_tree* node = malloc(sizeof(code_tree));
  node->left = NULL;
  node->right = NULL;
  node->data = c;
  node->weight = w;
  return node;
}

// Create a new internal node of a code tree with
// left successor l and right successor r.
code_tree* make_fork(code_tree* l, code_tree* r) {
  code_tree* node = malloc(sizeof(code_tree));
  node->left = l;
  node->right = r;
  node->weight = weight_of_tree(l) + weight_of_tree(r);
  node->data = 0;
  return node;
}

// Create a code tree from the given table of weights.
// The size of weights should be 128 (the number of ASCII characters).
code_tree* create_code_tree(int* weights) {
  minheap* heap = minheap_create(128);

  for (int i = 0; i < 128; i++) {
    if (weights[i] > 0)
      minheap_add(heap, make_leaf(i, weights[i]), weights[i]);
  }

  code_tree* root = NULL;

  switch (minheap_get_count(heap)) {
  case 0:
    break;
  case 1: {
    // make sure there are at least two nodes in the tree
    code_tree* left = minheap_delete_min(heap);
    code_tree* right = make_leaf(((unsigned char) left->data + 1) % 128, 1);
    root = make_fork(left, right);
    break;
  }
  default: 
    while (minheap_get_count(heap) != 1) {
      code_tree* left = minheap_delete_min(heap);
      code_tree* right = minheap_delete_min(heap);
      root = make_fork(left, right);
      minheap_add(heap, root, root->weight);
    }
    root = (code_tree*) minheap_delete_min(heap);
  }
  
  minheap_delete(heap);
  
  return root;
}

// Free all nodes in the code tree rooted at node.
void delete_code_tree(code_tree* node) {
  if (node == NULL) return;
  delete_code_tree(node->left);
  delete_code_tree(node->right);
  free(node);
}

void create_code_table_worker(code_tree* root, code* tbl, unsigned char* arr, int l) {
  if (root->left == NULL) {
    assert (l < 128);

    // Copy arr into tbl[root->data].bits
    for (int i = 0; i < l / 8 + 1; i++)
      tbl[root->data].bits[i] = arr[i];

    // Set unused bits in last byte to 0 -- not strictly needed
    if (l % 8 != 0) {
      for (int i = 0; i < 8 - (l % 8); i++)
        tbl[root->data].bits[l / 8] &= ~(1 << i);
    }
    
    tbl[root->data].len = l;
  } else {
    // Set next bit to 0
    arr[l / 8] &= ~(1 << (7 - (l % 8)));
    create_code_table_worker(root->left, tbl, arr, l + 1);
    // Set next bit to 1
    arr[l / 8] |= 1 << (7 - (l % 8));
    create_code_table_worker(root->right, tbl, arr, l + 1);
  }
}

// Populate the code table tbl with the codes represented by
// the code tree rooted at r.
void create_code_table(code_tree* r, code* tbl) {
  if (r == NULL) return;
  unsigned char arr[16];
  create_code_table_worker(r, tbl, arr, 0);
}

// Encode the contents of the text file in_file_name into the file
// out_file_name using the code table tbl.
void encode(char* in_file_name, char* out_file_name, code* tbl) {
  FILE* ifile = open_file(in_file_name, "r");
  FILE* ofile = open_file(out_file_name, "wb");
  
  char read_buffer[SIZE];

  // next byte to be written
  unsigned char next = 0;

  // number of bits currently used in next
  int next_len = 0;
  
  while (fgets(read_buffer, sizeof(read_buffer), ifile)) {
    for (int i = 0; read_buffer[i] != '\0'; ++i) {
      // Get next character from input stream as well as its code
      char c = read_buffer[i];
      int len = tbl[c].len;
      unsigned char* code = tbl[c].bits;

      // Write out code byte by byte
      for (int curr = 0; curr < len; curr += 8) {
        // pack as much of code[curr / 8] into next as possible 
        next |= code[curr / 8] >> next_len;

        // ready to write next byte ?
        if (curr + 8 <= len + next_len) {
          putc(next, ofile);

          next = code[curr / 8] << (8 - next_len);
          if (curr + 8 > len) {
            next_len = (len + next_len) % 8;
          }
        } else {
          next_len = next_len + (len - curr);
        }
      }
    }
  }

  // Write out remaining byte if anything left
  if (next_len > 0) {
    putc(next, ofile);
    putc(next_len, ofile);
  } else {
    putc(8, ofile);
  }
  
  fclose(ifile);
  fclose(ofile);
}

code_tree* decode_byte(code_tree* root, FILE* ofile, code_tree* curr, unsigned char b, int l, int lb) {
  if (curr == NULL) {
    fprintf(stderr, "Error: Encountered unexpected code during decoding.\n");
    abort();
  }
  if (curr->left != NULL) {
    if (l == lb) {
      return curr;
    } else {
      code_tree* next = b & (1 << (l - 1)) ? curr->right : curr->left;
      return decode_byte(root, ofile, next, b, l - 1, lb);
    }
  } else {
    putc(curr->data, ofile);
    return decode_byte(root, ofile, root, b, l, lb);
  }
}

// Decode the contents of the file in_file_name using the given code tree
// and write the resulting plain text into the file out_file_name.
void decode(char* in_file_name, char* out_file_name, code_tree* root) {
  FILE* ifile = open_file(in_file_name, "rb");
  FILE* ofile = open_file(out_file_name, "w");

  // Use two read buffers to detect when end of file has been reached
  unsigned char read_buffer1[SIZE];
  unsigned char read_buffer2[SIZE];

  unsigned char* buffer1 = read_buffer1;
  unsigned char* buffer2 = read_buffer2;
  
  int size = fread(buffer1, 1, SIZE, ifile);

  code_tree* curr = root;
  
  while (size > 1) {
    char last_len = 8;
    int size1 = size;
    if (size < SIZE) {
      // Definitely reached end of input file in previous fread
      last_len = buffer1[size - 1];
      size1 = size - 1;
      size = 0;
    } else {
      // Possibly more data to read
      size = fread(buffer2, 1, SIZE, ifile);
      if (size == 0) {
        // Reached end of input in previous fread
        last_len = buffer1[SIZE - 1];
        size1 = size1 - 1;
      } else if (size == 1) {
        // Only final byte was left to be read
        last_len = buffer2[0];
      }
    }

    for (int i = 0; i < size1 - 1; i++) {
      curr = decode_byte(root, ofile, curr, buffer1[i], 8, 0);
    }
    curr = decode_byte(root, ofile, curr, buffer1[size1 - 1], 8, 8 - last_len);

    // Swap buffer pointers
    unsigned char* temp = buffer2;
    buffer2 = buffer1;
    buffer1 = temp;
  }

  fclose(ifile);
  fclose(ofile);
}


#define ENCODE 1
#define DECODE 2

// Parse command line arguments
int parse_params(int argc, char** argv, int* mode, char** in_file_name, char** weight_file_name, char** out_file_name) {
  char c = 0;
  while ((c = getopt (argc, argv, "e:d:w:o:")) != -1) {
    switch (c) {
    case 'e':
      *mode |= ENCODE;
      *in_file_name = optarg;
      break;
    case 'd':
      *mode |= DECODE;
      *in_file_name = optarg;
      break;
    case 'w':
      *weight_file_name = optarg;
      break;
    case 'o':
      *out_file_name = optarg;
      break;
    case '?':
      switch (optopt) {
      case 'e':
      case 'd':
      case 'w':
      case 'o':
        fprintf(stderr, "Option -%c must be followed by a file name.\n", optopt);
        break;
      default:
        break;
      }
    default:
      return -1;
    }
  }

  if (!*mode) {
    fprintf(stderr, "Expected either -e or -d.\n");
    return -1;
  }
  
  if (!(*mode ^ (ENCODE | DECODE))) {
    fprintf(stderr, "Options -e and -d are exclusive.\n");
    return -1;
  }

  if (!*in_file_name) {
    fprintf(stderr, "Path to plain text file must be specified.\n");
    return -1;
  }
  if (!*weight_file_name) {
    fprintf(stderr, "Path to key file must be specified.\n");
    return -1;
  }
  if (!*out_file_name) {
    fprintf(stderr, "Path to output file must be specified.\n");
    return -1;
  }
  
  return 0;
}

void print_usage(char* name) {
  printf("Usage: %s <mode> <file> -w <file> -o <file>\nwhere <mode> is\n"
         "  -d decode file using given weight file\n"
         "  -e encode file\n", name);

}

// The main function of the application.
int main(int argc, char** argv) {
  int error_code = 0;
  int mode = 0;
  char* weight_file_name = NULL;
  char* in_file_name = NULL;
  char* out_file_name = NULL;

  error_code = parse_params(argc, argv, &mode, &in_file_name, &weight_file_name, &out_file_name);

  if (error_code) {
    print_usage(argv[0]);
    return error_code;
  }

  if (mode & ENCODE) {
    count_occurrences(in_file_name, weights);
    code_tree* root = create_code_tree(weights);
    create_code_table(root, ctable);
    write_weight_table(weight_file_name, weights);
    encode(in_file_name, out_file_name, ctable);
    delete_code_tree(root);
  } else {
    read_weight_table(weight_file_name, weights);
    code_tree* root = create_code_tree(weights);
    decode(in_file_name, out_file_name, root);
    delete_code_tree(root);
  }
  
  
  return 0;
}
