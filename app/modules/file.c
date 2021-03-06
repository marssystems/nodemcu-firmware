// Module for interfacing with file system

#include "module.h"
#include "lauxlib.h"
#include "platform.h"

#include "c_types.h"
#include "flash_fs.h"
#include "c_string.h"

static volatile int file_fd = FS_OPEN_OK - 1;

// Lua: open(filename, mode)
static int file_open( lua_State* L )
{
  size_t len;
  if((FS_OPEN_OK - 1)!=file_fd){
    fs_close(file_fd);
    file_fd = FS_OPEN_OK - 1;
  }

  const char *fname = luaL_checklstring( L, 1, &len );
  luaL_argcheck(L, len < FS_NAME_MAX_LENGTH && c_strlen(fname) == len, 1, "filename invalid");

  const char *mode = luaL_optstring(L, 2, "r");

  file_fd = fs_open(fname, fs_mode2flag(mode));

  if(file_fd < FS_OPEN_OK){
    file_fd = FS_OPEN_OK - 1;
    lua_pushnil(L);
  } else {
    lua_pushboolean(L, 1);
  }
  return 1; 
}

// Lua: close()
static int file_close( lua_State* L )
{
  if((FS_OPEN_OK - 1)!=file_fd){
    fs_close(file_fd);
    file_fd = FS_OPEN_OK - 1;
  }
  return 0;  
}

// Lua: format()
static int file_format( lua_State* L )
{
  size_t len;
  file_close(L);
  if( !fs_format() )
  {
    NODE_ERR( "\n*** ERROR ***: unable to format. FS might be compromised.\n" );
    NODE_ERR( "It is advised to re-flash the NodeMCU image.\n" );
    luaL_error(L, "Failed to format file system");
  }
  else{
    NODE_ERR( "format done.\n" );
  }
  return 0; 
}

#if defined(BUILD_SPIFFS)

extern spiffs fs;

// Lua: list()
static int file_list( lua_State* L )
{
  spiffs_DIR d;
  struct spiffs_dirent e;
  struct spiffs_dirent *pe = &e;

  lua_newtable( L );
  SPIFFS_opendir(&fs, "/", &d);
  while ((pe = SPIFFS_readdir(&d, pe))) {
    // NODE_ERR("  %s size:%i\n", pe->name, pe->size);
    lua_pushinteger(L, pe->size);
    lua_setfield( L, -2, pe->name );
  }
  SPIFFS_closedir(&d);
  return 1;
}

static int file_seek (lua_State *L) 
{
  static const int mode[] = {FS_SEEK_SET, FS_SEEK_CUR, FS_SEEK_END};
  static const char *const modenames[] = {"set", "cur", "end", NULL};
  if((FS_OPEN_OK - 1)==file_fd)
    return luaL_error(L, "open a file first");
  int op = luaL_checkoption(L, 1, "cur", modenames);
  long offset = luaL_optlong(L, 2, 0);
  op = fs_seek(file_fd, offset, mode[op]);
  if (op < 0)
    lua_pushnil(L);  /* error */
  else
    lua_pushinteger(L, fs_tell(file_fd));
  return 1;
}

// Lua: exists(filename)
static int file_exists( lua_State* L )
{
  size_t len;
  const char *fname = luaL_checklstring( L, 1, &len );    
  luaL_argcheck(L, len < FS_NAME_MAX_LENGTH && c_strlen(fname) == len, 1, "filename invalid");

  spiffs_stat stat;
  int rc = SPIFFS_stat(&fs, (char *)fname, &stat);

  lua_pushboolean(L, (rc == SPIFFS_OK ? 1 : 0));

  return 1;
}

// Lua: remove(filename)
static int file_remove( lua_State* L )
{
  size_t len;
  const char *fname = luaL_checklstring( L, 1, &len );    
  luaL_argcheck(L, len < FS_NAME_MAX_LENGTH && c_strlen(fname) == len, 1, "filename invalid");
  file_close(L);
  SPIFFS_remove(&fs, (char *)fname);
  return 0;  
}

// Lua: flush()
static int file_flush( lua_State* L )
{
  if((FS_OPEN_OK - 1)==file_fd)
    return luaL_error(L, "open a file first");
  if(fs_flush(file_fd) == 0)
    lua_pushboolean(L, 1);
  else
    lua_pushnil(L);
  return 1;
}
#if 0
// Lua: check()
static int file_check( lua_State* L )
{
  file_close(L);
  lua_pushinteger(L, fs_check());
  return 1;
}
#endif

// Lua: rename("oldname", "newname")
static int file_rename( lua_State* L )
{
  size_t len;
  if((FS_OPEN_OK - 1)!=file_fd){
    fs_close(file_fd);
    file_fd = FS_OPEN_OK - 1;
  }

  const char *oldname = luaL_checklstring( L, 1, &len );
  luaL_argcheck(L, len < FS_NAME_MAX_LENGTH && c_strlen(oldname) == len, 1, "filename invalid");
  
  const char *newname = luaL_checklstring( L, 2, &len );  
  luaL_argcheck(L, len < FS_NAME_MAX_LENGTH && c_strlen(newname) == len, 2, "filename invalid");

  if(SPIFFS_OK==myspiffs_rename( oldname, newname )){
    lua_pushboolean(L, 1);
  } else {
    lua_pushboolean(L, 0);
  }
  return 1;
}

// Lua: fsinfo()
static int file_fsinfo( lua_State* L )
{
  u32_t total, used;
  if (SPIFFS_info(&fs, &total, &used)) {
    return luaL_error(L, "file system failed");
  }
  NODE_DBG("total: %d, used:%d\n", total, used);
  if(total>0x7FFFFFFF || used>0x7FFFFFFF || used > total)
  {
    return luaL_error(L, "file system error");
  }
  lua_pushinteger(L, total-used);
  lua_pushinteger(L, used);
  lua_pushinteger(L, total);
  return 3;
}

#endif

// g_read()
static int file_g_read( lua_State* L, int n, int16_t end_char )
{
  if(n <= 0 || n > LUAL_BUFFERSIZE)
    n = LUAL_BUFFERSIZE;
  if(end_char < 0 || end_char >255)
    end_char = EOF;
  
  luaL_Buffer b;
  if((FS_OPEN_OK - 1)==file_fd)
    return luaL_error(L, "open a file first");

  luaL_buffinit(L, &b);
  char *p = luaL_prepbuffer(&b);
  int i;

  n = fs_read(file_fd, p, n);
  for (i = 0; i < n; ++i)
    if (p[i] == end_char)
    {
      ++i;
      break;
    }

  if(i==0){
    luaL_pushresult(&b);  /* close buffer */
    return (lua_objlen(L, -1) > 0);  /* check whether read something */
  }

  fs_seek(file_fd, -(n - i), SEEK_CUR);
  luaL_addsize(&b, i);
  luaL_pushresult(&b);  /* close buffer */
  return 1;  /* read at least an `eol' */ 
}

// Lua: read()
// file.read() will read all byte in file
// file.read(10) will read 10 byte from file, or EOF is reached.
// file.read('q') will read until 'q' or EOF is reached. 
static int file_read( lua_State* L )
{
  unsigned need_len = LUAL_BUFFERSIZE;
  int16_t end_char = EOF;
  size_t el;
  if( lua_type( L, 1 ) == LUA_TNUMBER )
  {
    need_len = ( unsigned )luaL_checkinteger( L, 1 );
    if( need_len > LUAL_BUFFERSIZE ){
      need_len = LUAL_BUFFERSIZE;
    }
  }
  else if(lua_isstring(L, 1))
  {
    const char *end = luaL_checklstring( L, 1, &el );
    if(el!=1){
      return luaL_error( L, "wrong arg range" );
    }
    end_char = (int16_t)end[0];
  }

  return file_g_read(L, need_len, end_char);
}

// Lua: readline()
static int file_readline( lua_State* L )
{
  return file_g_read(L, LUAL_BUFFERSIZE, '\n');
}

// Lua: write("string")
static int file_write( lua_State* L )
{
  if((FS_OPEN_OK - 1)==file_fd)
    return luaL_error(L, "open a file first");
  size_t l, rl;
  const char *s = luaL_checklstring(L, 1, &l);
  rl = fs_write(file_fd, s, l);
  if(rl==l)
    lua_pushboolean(L, 1);
  else
    lua_pushnil(L);
  return 1;
}

// Lua: writeline("string")
static int file_writeline( lua_State* L )
{
  if((FS_OPEN_OK - 1)==file_fd)
    return luaL_error(L, "open a file first");
  size_t l, rl;
  const char *s = luaL_checklstring(L, 1, &l);
  rl = fs_write(file_fd, s, l);
  if(rl==l){
    rl = fs_write(file_fd, "\n", 1);
    if(rl==1)
      lua_pushboolean(L, 1);
    else
      lua_pushnil(L);
  }
  else{
    lua_pushnil(L);
  }
  return 1;
}

static int file_fscfg (lua_State *L)
{
  lua_pushinteger (L, fs.cfg.phys_addr);
  lua_pushinteger (L, fs.cfg.phys_size);
  return 2;
}

// Module function map
static const LUA_REG_TYPE file_map[] = {
  { LSTRKEY( "list" ),      LFUNCVAL( file_list ) },
  { LSTRKEY( "open" ),      LFUNCVAL( file_open ) },
  { LSTRKEY( "close" ),     LFUNCVAL( file_close ) },
  { LSTRKEY( "write" ),     LFUNCVAL( file_write ) },
  { LSTRKEY( "writeline" ), LFUNCVAL( file_writeline ) },
  { LSTRKEY( "read" ),      LFUNCVAL( file_read ) },
  { LSTRKEY( "readline" ),  LFUNCVAL( file_readline ) },
  { LSTRKEY( "format" ),    LFUNCVAL( file_format ) },
#if defined(BUILD_SPIFFS)
  { LSTRKEY( "remove" ),    LFUNCVAL( file_remove ) },
  { LSTRKEY( "seek" ),      LFUNCVAL( file_seek ) },
  { LSTRKEY( "flush" ),     LFUNCVAL( file_flush ) },
  { LSTRKEY( "rename" ),    LFUNCVAL( file_rename ) },
  { LSTRKEY( "fsinfo" ),    LFUNCVAL( file_fsinfo ) },
  { LSTRKEY( "fscfg" ),     LFUNCVAL( file_fscfg ) },
  { LSTRKEY( "exists" ),    LFUNCVAL( file_exists ) },  
#endif
  { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(FILE, "file", file_map, NULL);
