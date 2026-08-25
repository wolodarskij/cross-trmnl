/*
** luac — compile cross-trmnl Lua scripts to bytecode the device will load.
**
** Built from the firmware's own vendored lib/Lua, which is the whole point:
** lundump.c's checkHeader verifies sizeof(Instruction), sizeof(lua_Integer)
** and sizeof(lua_Number) against the loading VM, and luaconf.h hard-codes
** LUA_32BITS=1 — so a stock host luac (64-bit integers, doubles) produces
** bytecode the ESP32-C3 rejects, while this one cannot, because it reads the
** same header the firmware does. There is no flag to get wrong.
**
** This is not upstream's luac. That one carries a bytecode lister and
** disassembler we have no use for, and would be one more vendored upstream
** file to keep in sync. Compile, strip, dump, verify — nothing else.
**
**   luac [-s] -o <out.luac> <in.lua>   compile (-s drops debug info)
**   luac --check <file>...             verify each file loads as bytecode
**   luac --info                        print this build's numeric config
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

static const char* kUsage =
    "usage:\n"
    "  luac [-s] -o <out.luac> <in.lua>   compile (-s strips debug info)\n"
    "  luac --check <file>...             verify each file loads as bytecode\n"
    "  luac --info                        print this build's numeric config\n";

static int writer(lua_State* L, const void* p, size_t size, void* ud) {
  (void)L;
  if (size == 0) return 0;
  return fwrite(p, size, 1, (FILE*)ud) != 1;
}

/* Every failure path prints to stderr and returns non-zero: this runs inside a
** tree build, where a silent bad output is far worse than a loud stop. */
static int fail(lua_State* L, const char* what) {
  const char* msg = (L != NULL) ? lua_tostring(L, -1) : NULL;
  fprintf(stderr, "luac: %s%s%s\n", what, msg ? ": " : "", msg ? msg : "");
  return 1;
}

static int compile(const char* in, const char* out, int strip) {
  lua_State* L;
  FILE* f;
  int status;

  L = luaL_newstate();
  if (L == NULL) {
    fprintf(stderr, "luac: out of memory\n");
    return 1;
  }
  /* Mode "t": refuse a file that is already bytecode rather than copying it
  ** through, which would silently defeat a re-run with different flags. */
  if (luaL_loadfilex(L, in, "t") != LUA_OK) {
    int rc = fail(L, "cannot compile");
    lua_close(L);
    return rc;
  }

  f = fopen(out, "wb");
  if (f == NULL) {
    fprintf(stderr, "luac: cannot open %s for writing\n", out);
    lua_close(L);
    return 1;
  }
  status = lua_dump(L, writer, f, strip);
  if (fclose(f) != 0 || status != 0) {
    fprintf(stderr, "luac: write failed for %s\n", out);
    remove(out); /* a truncated .luac is worse than none */
    lua_close(L);
    return 1;
  }
  lua_close(L);
  return 0;
}

/* Loads as *binary only*. Passing a .lua source here has to fail, or --check
** would pass on a tree that was never compiled. */
static int check(const char* path) {
  lua_State* L = luaL_newstate();
  int ok;
  if (L == NULL) {
    fprintf(stderr, "luac: out of memory\n");
    return 1;
  }
  ok = (luaL_loadfilex(L, path, "b") == LUA_OK);
  if (!ok) {
    fprintf(stderr, "luac: %s rejected: %s\n", path, lua_tostring(L, -1));
  } else {
    printf("ok  %s\n", path);
  }
  lua_close(L);
  return ok ? 0 : 1;
}

static void info(void) {
  printf("%s\n", LUA_RELEASE);
  printf("  lua_Integer  %d bytes\n", (int)sizeof(lua_Integer));
  printf("  lua_Number   %d bytes%s\n", (int)sizeof(lua_Number),
         (sizeof(lua_Number) == 4) ? "  (float)" : "  (double)");
  printf("  LUA_32BITS   %d\n", (int)LUA_32BITS);
  printf("These three are what lundump.c checkHeader compares against the\n"
         "loading VM. They must match the firmware exactly.\n");
}

int main(int argc, char** argv) {
  int i;
  int strip = 0;
  const char* out = NULL;
  const char* in = NULL;

  if (argc < 2) {
    fputs(kUsage, stderr);
    return 1;
  }
  if (strcmp(argv[1], "--info") == 0) {
    info();
    return 0;
  }
  if (strcmp(argv[1], "--check") == 0) {
    int rc = 0;
    if (argc < 3) {
      fputs(kUsage, stderr);
      return 1;
    }
    for (i = 2; i < argc; i++) {
      if (check(argv[i]) != 0) rc = 1;
    }
    return rc;
  }

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--strip") == 0) {
      strip = 1;
    } else if (strcmp(argv[i], "-o") == 0) {
      if (++i >= argc) {
        fputs(kUsage, stderr);
        return 1;
      }
      out = argv[i];
    } else if (in == NULL) {
      in = argv[i];
    } else {
      fprintf(stderr, "luac: unexpected argument '%s'\n", argv[i]);
      fputs(kUsage, stderr);
      return 1;
    }
  }
  if (in == NULL || out == NULL) {
    fputs(kUsage, stderr);
    return 1;
  }
  return compile(in, out, strip);
}
