extern int kclose(int);
extern int kdial(char *, char *, char *, int *);
extern int kannounce(char *, char *, size_t);
extern void kerrstr(char *);
extern void kgerrstr(char *);
extern int kopen(char *, int);
extern long kread(int, void *, long);
extern long kseek(int, vlong, int);
extern long kwrite(int, void *, long);
