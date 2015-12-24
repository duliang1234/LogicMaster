#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <stdio.h>
#include <stdlib.h>

#include "platform.h"
#include "makeparam.h"

/* spi LIB type,used for create param */
#define SPI_LIB_TYPE	'g'

#define LSPI_MODE0 MAKE_P(SPI_LIB_TYPE,1,SPI_MODE0)
#define LSPI_MODE1 MAKE_P(SPI_LIB_TYPE,2,SPI_MODE1)
#define LSPI_MODE2 MAKE_P(SPI_LIB_TYPE,3,SPI_MODE2)
#define LSPI_MODE3 MAKE_P(SPI_LIB_TYPE,4,SPI_MODE3)
#define is_lspi_mode(mode) ( (P_TYPE(mode)==SPI_LIB_TYPE) && \
                             (P_NUM(mode)>=1)&&(P_NUM(mode)<=4) )

#define LSPI_MSBFIRST MAKE_P(SPI_LIB_TYPE,5,SPI_MSBFIRST)
#define LSPI_LSBFIRST MAKE_P(SPI_LIB_TYPE,6,SPI_LSBFIRST)
#define is_lspi_order(order) ( (P_TYPE(order)==SPI_LIB_TYPE) && \
                               (P_NUM(order)>=5)&&(P_NUM(order)<=6) )
#define LSPI_HIGH		-1
#define LSPI_LOW 		-2 /* spi token */             

/* lua functions:
 * spi.init(mode,order)
 * spi.init(mode,order,bauddiv)
 * spi HAL functions:
 * ==> int spi_init(int mode,int order,int bauddiv)
 * == ==>bauddiv from 0 to 7.
 */
static int lspi_init(lua_State *L)  
{
	int mode,order,paramcount;
	int bauddiv = 1;
	
	paramcount=lua_gettop(L);
	mode=luaL_checkinteger(L,1);
	order=luaL_checkinteger(L,2);
	if(paramcount>2) 
		bauddiv=luaL_checkinteger(L,3);
	
	if(!is_lspi_mode(mode) || !is_lspi_order(order)){
		lua_pushstring(L,"param error! spi.init(mode,order)\n"
										 "or spi.init(mode,order,bauddiv)");
		lua_error(L);
	}
	if(spi_init(P_DATA(mode),P_DATA(order),bauddiv)){
		lua_pushstring(L,"error! 'bauddiv' must be selected from 0 ~ 7.");
		lua_error(L);
	}
	
	return 0;
}                                                         

/* lua function:
 * spi.mode(mode)
 * spi.order(order)
 * spi.div(bauddiv)
 * ==> int spi_mode(int mode)
 * ==> int spi_order(int order)
 * ==> int spi_div(int bauddiv)
 */
static int lspi_mode(lua_State *L)
{
	int mode = luaL_checkinteger(L,1);
	if(!is_lspi_mode(mode)){
		lua_pushstring(L,"param error! Can be spi.MODE0 ~ spi.MODE3");
		lua_error(L);
	}
	spi_mode(P_DATA(mode));
	return 0;
}

static int lspi_order(lua_State *L)
{
	int order=luaL_checkinteger(L,1);
	if(!is_lspi_order(order)){
		lua_pushstring(L,"param error! Can be spi.LSBFIRST or spi.MSBFIRST.");
		lua_error(L);
	}
	spi_order(P_DATA(order));
	return 0; 
}

static int lspi_div(lua_State *L)
{
	int div=luaL_checkinteger(L,1);
	if(spi_div(div)){
		lua_pushstring(L,"param error! Can be 0 ~ 7.");
		lua_error(L);
	}
	return 0;
}

/* 
 * spi.info()
 * >  print spi infomation to console.including spi mode,spi data order and
 * >  spi clock infomation.
 * SPI HAL function:
 * ==> int spi_info(void);
 * == ===>return value:
 * --------------------------------------------------------
 * 31  (ClockFreq)    4 | 3(Hz/MHz) | 2(order) | 1~0(mode)
 * --------------------------------------------------------
 * 3(KHz/MHz): 0=Hz,1=MHz
 * 2(order):   SPI_MSBFIRST or SPI_LSBFIRST
 * --------------------------------------------------------
 */
#define info_freq(info)  ( (info)>>4 )
#define info_hz(info)    ( ((info)>>3)&0x01)
#define info_order(info) ( ((info)>>2)&0x01)
#define info_mode(info)  ( (info) & 0x03 )

const char *hzinfo[]={"Hz","MHz"};
const char *orderinfo[]={"MSBFIRST","LSBFIRST"};
const char *modeinfo[]={"MODE0","MODE1","MODE2","MODE3"};

static int lspi_info(lua_State *L)
{
	int freq,hz,order,mode;
	int info = spi_info();
	
	freq=info_freq(info);
	hz=info_hz(info);
	order=info_order(info);
	mode=info_mode(info);
	
	lua_pushfstring(L,"spi freq: %d %s, %s, %s",
		freq,hzinfo[hz],modeinfo[mode],orderinfo[order]);
	return 1;
}

/* lua function: 
 * spi.dummybyte(); get spi's dummy byte
 * spi.dummybyte(byte); set spi's dummy byte
 * > dummy byte is the data writed to MOSI when stm32 try 
 * > read data from MISO.
 */
static int dummybyte = 0xff;
static int lspi_dummybyte(lua_State *L)
{
	int param,paramcount;
		
	paramcount=lua_gettop(L);
	if(paramcount>0){ /* set dummy byte */
		param=luaL_checkinteger(L,1);
		if( (param>0xff)||(param<0) ){
			lua_pushstring(L,"param error! dummy byte MUST be 0 ~ 255!");
			lua_error(L);
		}
		else dummybyte=param;
		return 0;
	}
	
	lua_pushinteger(L,dummybyte);/* get dummy byte */
	return 1;
}

/* lua function:
 * spi.write(data_or_token,...)
 * data must from 0 to 255.(include 0 and 255)
 * token can be selected from LSPI_HIGH or LSPI_LOW.
 * LSPI_HIGH means pull up cs pin.
 * LSPI_LOW  means pull down cs pin.
 */
static int lspi_write(lua_State *L)
{
	int i,param,paramcount;
	
	paramcount = lua_gettop(L);
	for(i=0;i<paramcount;i++){
		switch(param=luaL_checkinteger(L,i+1)) /* lua index from 1 NOT 0 */
		{
			case LSPI_HIGH: spi_cshigh();break;
			case LSPI_LOW: spi_cslow(); break;
			default: spi_transfer(param);break;
		}
	}
	return 0;
}

/* lua fucntion:
 * spi.read(readnum_or_token,...)
 * 'readnum' MUST > 0. token can be LSPI_HIGH or LSPI_LOW
 * this is a multi-return function in lua. return value is the actually data.
 */
static int lspi_read(lua_State *L)
{
	int i,j;
	int param,paramcount,*parambuffer;
	int count = 0;
	
	paramcount=lua_gettop(L);
	parambuffer=(int *)malloc(sizeof(int)*paramcount);
	for(i=0;i<paramcount;i++){ /* saving param to 'parambuffer' */
		if(!lua_isinteger(L,i+1)) goto notint;/* lua index from 1 NOT 0 */
		parambuffer[i]=lua_tointeger(L,i+1);
	}
	
	/* analysis param */
	for(i=0;i<paramcount;i++){
		switch(param=parambuffer[i]){
		case LSPI_HIGH: spi_cshigh(); break;
		case LSPI_LOW: spi_cslow();  break;
		default: /* need read count of 'param' data */
			if(!lua_checkstack(L,param)) goto nomem;/* memory isn't enough */
			for(j=0;j<param;j++) lua_pushinteger(L,spi_transfer(dummybyte));
			count += param;
			break;
		}
	}
	
	free(parambuffer);
	return count;
	
	notint:
		free(parambuffer);
		lua_pushstring(L,"param error! 'readnum' MUST > 0,"
										 "token can be spi.HIGH or spi.LOW.");
		lua_error(L);
		return 0;
	
	nomem:
		free(parambuffer);
		lua_settop(L,0);/* clear stack */
		lua_pushstring(L,"Error! 'readnum' is too larger, memory isn't enough!");
		lua_error(L);
		return 0;
}

/* lua function:
 * spi.transfer(data_or_token,...)
 * >sending data by using spi bus's MOSI,buffed data on MISO. When sending
 * >complete, return all buffed data.
 */
static int lspi_transfer(lua_State *L)
{
	int i,j;
	int param,paramcount,*parambuffer;
	int count = 0;
	
	paramcount=lua_gettop(L);
	parambuffer=(int *)malloc(sizeof(int)*paramcount);
	for(i=0;i<paramcount;i++){ /* saving param to 'parambuffer' */
		if(!lua_isinteger(L,i+1)) goto notint;/* lua index from 1 NOT 0 */
		parambuffer[i]=lua_tointeger(L,i+1);
	}
	
	/* 
	 * Maybe has token in param, but in order to make programe easier,
	 * I just ignore all token.
	 */
	if(!lua_checkstack(L,paramcount)) goto nomem;
	for(i=0;i<paramcount;i++){
		switch(param=parambuffer[i]){
		case LSPI_HIGH: spi_cshigh(); break;
		case LSPI_LOW: spi_cslow();  break;
		default: lua_pushinteger(L,spi_transfer(param)); count++; break;
		}
	}
	
	free(parambuffer);
	return count;
	
	notint:
		free(parambuffer);
		lua_pushstring(L,"param error! 'readnum' MUST > 0,"
										 "token can be spi.HIGH or spi.LOW.");
		lua_error(L);
		return 0;
		
	nomem:
		free(parambuffer);
		lua_settop(L,0);/* clear stack */
		lua_pushstring(L,"Error! memory isn't enough!");
		lua_error(L);
		return 0;
}

static const luaL_Reg spiLib[] = {
	{"init",lspi_init},
	{"mode",lspi_mode},
	{"order",lspi_order},
	{"div",lspi_div},
	{"info",lspi_info},
	{"dummybyte",lspi_dummybyte},
	{"write",lspi_write},
	{"read",lspi_read},
	{"transfer",lspi_transfer},
	
	{"MODE0",NULL},
	{"MODE1",NULL},
	{"MODE2",NULL},
	{"MODE3",NULL},
	{"MSBFIRST",NULL},
	{"LSBFIRST",NULL},
	{"HIGH",NULL},
	{"LOW",NULL},
	
	{NULL,NULL},
};

LUAMOD_API int luaopen_spi (lua_State *L)
{
	luaL_newlib(L,spiLib);

  lua_pushinteger(L,LSPI_MODE0);
  lua_setfield(L,-2,"MODE0");
  lua_pushinteger(L,LSPI_MODE1);
  lua_setfield(L,-2,"MODE1");
  lua_pushinteger(L,LSPI_MODE2);
  lua_setfield(L,-2,"MODE2");
  lua_pushinteger(L,LSPI_MODE3);
  lua_setfield(L,-2,"MODE3");
  lua_pushinteger(L,LSPI_MSBFIRST);
  lua_setfield(L,-2,"MSBFIRST");
  lua_pushinteger(L,LSPI_LSBFIRST);
  lua_setfield(L,-2,"LSBFIRST");
  lua_pushinteger(L,LSPI_HIGH);
  lua_setfield(L,-2,"HIGH");
  lua_pushinteger(L,LSPI_LOW);
  lua_setfield(L,-2,"LOW");
    
  return 1;
}















