/* simple test program for blkclone UUID facilities */

#include <stdio.h>

#include "uuid.h"

struct {
  char text_uuid[40];
} *test, testcases[] = {
  {"1ef6f634-3cd9-4ac7-ada1-cdc9610928b4"},
  {"f155be4e-d7e7-451f-8bc5-d832f5dd3557"},
  {"44415b6f-d836-4335-8828-9858a53f72fc"},
  {"1a654e2e-a83e-4fc0-85f1-0488cbfa59a2"},

  {"ee6f2264-a214-429c-af88-9ddad1ace78e"},
  {"bc798c43-7896-4ea6-92f2-198150af8e01"},
  {"6a04014b-20db-43dd-942c-3471c38a27a2"},
  {"d4b4029f-d028-4874-9926-1fbb7ad072e5"},

  {"c55bfbff-ca1c-4bc9-bfd8-60b88f06f01e"},
  {"fd616e43-c352-4843-8389-0f72164cb4bb"},
  {"0e24a614-6be4-4082-a659-b84f3a77b9f1"},
  {"fbd55fe9-93f1-4c21-a38d-bc942f4c6ddf"},

  {"fa489e0b-acc0-49be-bb8e-f1c34ec1d255"},
  {"5ad6d2b2-a9f9-4d52-85fc-fbe3ed767300"},
  {"643be489-f047-4942-9140-5252814fae27"},
  {"ed53b653-b359-4419-b6be-37a575049c31"},

  {{0}}};

int main(void) {
  uuid_t uuid;
  int cnt = 0;

  test = testcases;
  while (test->text_uuid[0]) {
    printf("%4d %% %s # -> %% ",cnt++,test->text_uuid);
    parse_uuid(test->text_uuid, &uuid);
    print_uuid(stdout,&uuid);
    puts(" #");
    test++;
  }

  return 0;
}
