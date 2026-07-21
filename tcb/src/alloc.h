#ifndef _ALLOC_H

int exit_ALLOC(char *name);
void init_ALLOC(void);

#define _ALLOC_H
#else

int exit_ALLOC(char *name)
{
  return 0;
}

void init_ALLOC(void)
{
}

#endif
