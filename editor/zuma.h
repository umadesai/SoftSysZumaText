#ifndef _ZUMA_H
#define _ZUMA_H

struct editorRow;
char *editorPrompt(char*);
void editorSelectSyntaxHighlight();
void editorUpdateSyntax(struct editorRow*);

#endif
