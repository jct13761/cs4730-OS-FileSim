
int rand_string(char *str, size_t size);
void set_bit(char *array, int index, char value);
char get_bit(char *array, int index);
int get_free_inode();
int get_free_block();
void set_free_inode(int i);
void set_free_block(int i);
int format_timeval(struct timeval *tv, char *buf, size_t sz);
