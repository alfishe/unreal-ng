#include <stdio.h>
char *alloc(nbytes)
unsigned nbytes;
{
	struct _header *p, *q, *cp;
	int nunits; 
	nunits = 1 + (nbytes + (sizeof (_base) - 1)) / sizeof (_base);
	if ((q = _allocp) == NULL) {
		_base._ptr = _allocp = q = &_base;
		_base._size = 0;
	 }
	for (p = q -> _ptr; ; q = p, p = p -> _ptr) {
		if (p -> _size >= nunits) {
			_allocp = q;
			if (p -> _size == nunits)
				_allocp->_ptr = p->_ptr;
			else {
				q = _allocp->_ptr = p + nunits;
				q->_ptr = p->_ptr;
				q->_size = p->_size - nunits;
				p -> _size = nunits;
			 }
			return p + 1;
		 }
		if (p == _allocp) {
			if ((cp = sbrk(nunits * sizeof (_base))) == ERROR)
				return NULL;
			cp -> _size = nunits; 
			free(cp+1);	/* remember: pointer arithmetic! */
			p = _allocp;
		}
	 }
}

free(ap)
struct _header *ap;
{
	struct _header *p, *q;

	p = ap - 1;	/* No need for the cast when "ap" is a struct ptr */

	for (q = &_base; q->_ptr != &_base; q = q -> _ptr)
		if (p > q && p < q -> _ptr)
			break;

	if (p + p -> _size == q -> _ptr) {
		p -> _size += q -> _ptr -> _size;
		p -> _ptr = q -> _ptr -> _ptr;
	 }
	else p -> _ptr = q -> _ptr;

	if (q + q -> _size == p) {
		q -> _size += p -> _size;
		q -> _ptr = p -> _ptr;
	 }
	else q -> _ptr = p;

	_allocp = q;
}

int fflush(fp)
FILE *fp;
{
	int i; char *p;

	if (fp <= 4) /*stdin, stdout, stderr?*/
		return OK;
	if (!(fp->_flags & _WRITE))
		return ERROR;

	if (fp->_nleft == (NSECTS * SECSIZ))
		return OK;

	i = NSECTS * SECSIZ - fp->_nleft;
	if (write(fp->_fd, fp->_buff, i) != i)
	{
		fp->_flags |= _ERR;
		return ERROR;
	}

	fp->_nleft = (NSECTS * SECSIZ);
	fp->_nextp = fp->_buff;
	return OK;
}

int fread(buf, size, count, fp)
char *buf;
unsigned size, count;
FILE *fp;
{
	int n_read, n_togo, cnt, i;

	n_togo = size * count;
	n_read = 0;
	if (fp->_flags & _EOF)
		return NULL;

	while (n_togo)
	{
		cnt = (n_togo <= fp->_nleft) ? n_togo : fp->_nleft; /* how many bytes we can get from buffer at once */
		movmem(fp->_nextp, buf, cnt); /* get them */
		fp->_nextp += cnt; /* source pointer */
		buf += cnt; /* destination pointer */
		fp->_nleft -= cnt; /* how many bytes remained loaded in buffer */
		n_togo -= cnt; /* how many bytes we still need */
		n_read += cnt;
		if (n_togo) /* we need more bytes */
		{
                        if ((cnt = read(fp->_fd, fp->_buff, NSECTS*SECSIZ)) <=0)
			{
				fp->_flags |= _EOF;
				goto text_test;
			}
			fp->_nleft = cnt;
			fp->_nextp = fp->_buff;
		}
	}
 text_test:
	if (fp->_flags & _TEXT)
	{
		i = min(n_read, SECSIZ);
		while (i--)
			if (*(buf-i) == CPMEOF)
			{
				fp->_flags |= _EOF;
				return (n_read - i);
			}
	}
	return (n_read/size);
}

int fwrite(buf, size, count, fp)
char *buf;
unsigned size, count;
FILE *fp;
{
	int n_done, n_togo, cnt;

	n_togo = size * count;
	n_done = 0;

	if (fp->_flags & _ERR)
		return NULL;

	while (n_togo)
	{
		cnt = (n_togo <= fp->_nleft) ? n_togo : fp->_nleft;
		movmem(buf, fp->_nextp, cnt);
		fp->_nextp += cnt;
		buf += cnt;
		fp->_nleft -= cnt;
		n_togo -= cnt;
		n_done += cnt;
		if (n_togo)
		{
			if ((cnt = write(fp->_fd, fp->_buff, NSECTS*SECSIZ)) <= 0)
			{
				fp->_flags |= _ERR;
				return ERROR;
			}
			fp->_nleft = (NSECTS * SECSIZ);
			fp->_nextp = fp->_buff;
		}
	}
	return (n_done/size);
}

unsigned strtouint(s)
char *s;
{
unsigned n;
char c;
n = 0;
while(c = *s++) {
        n = n*10;
        n += c-'0';
}
return n;
}

#define LOADBUFSZ 4096
char loadbuf[LOADBUFSZ];
FILE g_fpout;
int i;
int fd;

main(argc,argv)
char **argv;
{
FILE *fpin;
FILE *fpout;
unsigned size;
unsigned sizeremain;
unsigned sizetoread;
int parindex;
parindex = 3;
printf("Cut file. Usage: cut SIZE infile outfile1 outfile2...\n");

for (i = 0; i < argc; i++) printf("Arg #%d = %s\n",i,argv[i]);
if (argc < 4) return;

size = strtouint(argv[1]);
/*while(1);*/

/*fpout = fopen("myfile3.a","wb");*/
if ((fpin = alloc(sizeof(*fpin))) == NULL) return NULL;
fpin->_nextp = fpin->_buff;
/*fpout->_nleft = (NSECTS * SECSIZ);*/
/*fpout->_flags = _WRITE;*/
/*fpout->_fd = creat(argv[1]);*/
fpin->_nleft = 0;
fpin->_flags = _READ;
fpin->_fd = open(argv[2], 0);

while (1) {

        /*fp = fopen("ex.c","rb");*/
        if ((fpout = alloc(sizeof(*fpout))) == NULL) return NULL;
        fpout->_nextp = fpout->_buff;
        /*fp->_nleft = 0;*/
        /*fp->_fd = open(argv[2], 0);*/
        fpout->_nleft = (NSECTS * SECSIZ);
        fpout->_flags = _WRITE;
        fpout->_fd = creat(argv[parindex]);

        sizeremain = size;
        while (sizeremain) {
         sizetoread = sizeremain;
         if (sizetoread > LOADBUFSZ) sizetoread = LOADBUFSZ;
         i = fread(loadbuf, 1, sizetoread, fpin);
         sizeremain -= i;
         /*while (1);*/
         if (!i) break;
         fwrite(loadbuf, 1, i, fpout);
        }

        /*fclose(fpout);*/
        if (fflush(fpout) == ERROR) return ERROR;
        close(fpout->_fd);
        free(fpout);
        
        parindex++;
        if (parindex == argc) break;

}

/*fclose(fpin);*/
close(fpin->_fd);
free(fpin);
}
