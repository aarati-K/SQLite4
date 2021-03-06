/*
** 2002 February 23
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains the C functions that implement various SQL
** functions of SQLite.  
**
** There is only one exported symbol in this file - the function
** sqliteRegisterBuildinFunctions() found at the bottom of the file.
** All other code has file scope.
*/
#include "sqliteInt.h"
#include <stdlib.h>
#include <assert.h>
#include "vdbeInt.h"

/*
** Return the collating function associated with a function.
*/
static CollSeq *sqlite4GetFuncCollSeq(sqlite4_context *context){
  return context->pColl;
}

/*
** Implementation of the non-aggregate min() and max() functions
*/
static void minmaxFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  int i;
  int mask;    /* 0 for min() or 0xffffffff for max() */
  int iBest;
  CollSeq *pColl;

  assert( argc>1 );
  mask = sqlite4_context_appdata(context)==0 ? 0 : -1;
  pColl = sqlite4GetFuncCollSeq(context);
  assert( pColl );
  assert( mask==-1 || mask==0 );
  iBest = 0;
  if( sqlite4_value_type(argv[0])==SQLITE4_NULL ) return;
  for(i=1; i<argc; i++){
    int res, rc;
    if( sqlite4_value_type(argv[i])==SQLITE4_NULL ) return;
    rc = sqlite4MemCompare(argv[iBest], argv[i], pColl, &res);
    if( rc!=SQLITE4_OK ){
      sqlite4_result_error_code(context, rc);
      return;
    }
    if( (res^mask)>=0 ){
      testcase( mask==0 );
      iBest = i;
    }
  }
  sqlite4_result_value(context, argv[iBest]);
}

/*
** Return the type of the argument.
*/
static void typeofFunc(
  sqlite4_context *context,
  int NotUsed,
  sqlite4_value **argv
){
  const char *z = 0;
  UNUSED_PARAMETER(NotUsed);
  switch( sqlite4_value_type(argv[0]) ){
    case SQLITE4_INTEGER: z = "integer"; break;
    case SQLITE4_TEXT:    z = "text";    break;
    case SQLITE4_FLOAT:   z = "real";    break;
    case SQLITE4_BLOB:    z = "blob";    break;
    default:             z = "null";    break;
  }
  sqlite4_result_text(context, z, -1, SQLITE4_STATIC, 0);
}


/*
** Implementation of the length() function
*/
static void lengthFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  assert( argc==1 );
  UNUSED_PARAMETER(argc);
  switch( sqlite4_value_type(argv[0]) ){
    case SQLITE4_BLOB: {
      int nBlob;
      sqlite4_value_blob(argv[0], &nBlob);
      sqlite4_result_int(context, nBlob);
      break;
    };

    case SQLITE4_INTEGER:
    case SQLITE4_FLOAT:
    case SQLITE4_TEXT: {
      const char *z = sqlite4_value_text(argv[0], 0);
      if( z ){ 
        int nChar;
        for(nChar=0; *z; nChar++){
          SQLITE4_SKIP_UTF8(z);
        }
        sqlite4_result_int(context, nChar);
      }
      break;
    }

    default: {
      sqlite4_result_null(context);
      break;
    }
  }
}

/*
** Implementation of the abs() function.
**
** IMP: R-23979-26855 The abs(X) function returns the absolute value of
** the numeric argument X. 
*/
static void absFunc(sqlite4_context *context, int argc, sqlite4_value **argv){
  assert( argc==1 );
  UNUSED_PARAMETER(argc);

  switch( sqlite4_value_type(argv[0]) ){
    case SQLITE4_INTEGER: {
      i64 iVal = sqlite4_value_int64(argv[0]);
      if( iVal<0 ){
        if( (iVal<<1)==0 ){
          /* IMP: R-35460-15084 If X is the integer -9223372036854775807 then
          ** abs(X) throws an integer overflow error since there is no
          ** equivalent positive 64-bit two complement value. */
          sqlite4_result_error(context, "integer overflow", -1);
          return;
        }
        iVal = -iVal;
      } 
      sqlite4_result_int64(context, iVal);
      break;
    }
    case SQLITE4_NULL: {
      /* IMP: R-37434-19929 Abs(X) returns NULL if X is NULL. */
      sqlite4_result_null(context);
      break;
    }
    default: {
      /* Because sqlite4_value_num() returns 0.0 if the argument is not
      ** something that can be converted into a number, we have:
      ** IMP: R-57326-31541 Abs(X) return 0.0 if X is a string or blob that
      ** cannot be converted to a numeric value.  */
      sqlite4_num num = sqlite4_value_num(argv[0]);
      num.sign = 0;
      sqlite4_result_num(context, num);
      break;
    }
  }
}

/*
** Implementation of the substr() function.
**
** substr(x,p1,p2)  returns p2 characters of x[] beginning with p1.
** p1 is 1-indexed.  So substr(x,1,1) returns the first character
** of x.  If x is text, then we actually count UTF-8 characters.
** If x is a blob, then we count bytes.
**
** If p1 is negative, then we begin abs(p1) from the end of x[].
**
** If p2 is negative, return the p2 characters preceeding p1.
*/
static void substrFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  const char *z;
  const char *z2;
  int len;
  int p0type;
  i64 p1, p2;
  int negP2 = 0;

  assert( argc==3 || argc==2 );
  if( sqlite4_value_type(argv[1])==SQLITE4_NULL
   || (argc==3 && sqlite4_value_type(argv[2])==SQLITE4_NULL)
  ){
    return;
  }
  p0type = sqlite4_value_type(argv[0]);
  p1 = sqlite4_value_int(argv[1]);
  if( p0type==SQLITE4_BLOB ){
    z = sqlite4_value_blob(argv[0], &len);
    if( z==0 ) return;
  }else{
    z = sqlite4_value_text(argv[0], 0);
    if( z==0 ) return;
    len = 0;
    if( p1<0 ){
      for(z2=z; *z2; len++){
        SQLITE4_SKIP_UTF8(z2);
      }
    }
  }
  if( argc==3 ){
    p2 = sqlite4_value_int(argv[2]);
    if( p2<0 ){
      p2 = -p2;
      negP2 = 1;
    }
  }else{
    p2 = sqlite4_context_db_handle(context)->aLimit[SQLITE4_LIMIT_LENGTH];
  }
  if( p1<0 ){
    p1 += len;
    if( p1<0 ){
      p2 += p1;
      if( p2<0 ) p2 = 0;
      p1 = 0;
    }
  }else if( p1>0 ){
    p1--;
  }else if( p2>0 ){
    p2--;
  }
  if( negP2 ){
    p1 -= p2;
    if( p1<0 ){
      p2 += p1;
      p1 = 0;
    }
  }
  assert( p1>=0 && p2>=0 );
  if( p0type!=SQLITE4_BLOB ){
    while( *z && p1 ){
      SQLITE4_SKIP_UTF8(z);
      p1--;
    }
    for(z2=z; *z2 && p2; p2--){
      SQLITE4_SKIP_UTF8(z2);
    }
    sqlite4_result_text(context, (char*)z, (int)(z2-z), SQLITE4_TRANSIENT, 0);
  }else{
    if( p1+p2>len ){
      p2 = len-p1;
      if( p2<0 ) p2 = 0;
    }
    sqlite4_result_blob(context, (char*)&z[p1], (int)p2, SQLITE4_TRANSIENT, 0);
  }
}

/*
** Implementation of the round() function
*/
static void roundFunc(sqlite4_context *context, int argc, sqlite4_value **argv){
  int n = 0;                      /* Second argument to this function */
  assert( argc==1 || argc==2 );
  if( argc==2 ){
    if( SQLITE4_NULL==sqlite4_value_type(argv[1]) ) return;
    n = sqlite4_value_int(argv[1]);
    if( n>30 ) n = 30;
    if( n<0 ) n = 0;
  }
  if( sqlite4_value_type(argv[0])==SQLITE4_NULL ) return;

  sqlite4_result_num(context, sqlite4_num_round(sqlite4_value_num(argv[0]), n));
}

/*
** Allocate nByte bytes of space using sqlite4_malloc(). If the
** allocation fails, call sqlite4_result_error_nomem() to notify
** the database handle that malloc() has failed and return NULL.
** If nByte is larger than the maximum string or blob length, then
** raise an SQLITE4_TOOBIG exception and return NULL.
*/
static void *contextMalloc(sqlite4_context *context, i64 nByte){
  char *z;
  sqlite4 *db = sqlite4_context_db_handle(context);
  assert( nByte>0 );
  testcase( nByte==db->aLimit[SQLITE4_LIMIT_LENGTH] );
  testcase( nByte==db->aLimit[SQLITE4_LIMIT_LENGTH]+1 );
  if( nByte>db->aLimit[SQLITE4_LIMIT_LENGTH] ){
    sqlite4_result_error_toobig(context);
    z = 0;
  }else{
    z = sqlite4Malloc(db->pEnv, (int)nByte);
    if( !z ){
      sqlite4_result_error_nomem(context);
    }
  }
  return z;
}

/*
** Implementation of the upper() and lower() SQL functions.
*/
static void upperFunc(sqlite4_context *context, int argc, sqlite4_value **argv){
  char *z1;
  const char *z2;
  int i, n;
  UNUSED_PARAMETER(argc);
  z2 = sqlite4_value_text(argv[0], &n);
  if( z2 ){
    z1 = contextMalloc(context, ((i64)n)+1);
    if( z1 ){
      for(i=0; i<n; i++){
        z1[i] = (char)sqlite4Toupper(z2[i]);
      }
      sqlite4_result_text(context, z1, n, SQLITE4_DYNAMIC, 0);
    }
  }
}
static void lowerFunc(sqlite4_context *context, int argc, sqlite4_value **argv){
  char *z1;
  const char *z2;
  int i, n;
  UNUSED_PARAMETER(argc);
  z2 = sqlite4_value_text(argv[0], &n);
  if( z2 ){
    z1 = contextMalloc(context, ((i64)n)+1);
    if( z1 ){
      for(i=0; i<n; i++){
        z1[i] = sqlite4Tolower(z2[i]);
      }
      sqlite4_result_text(context, z1, n, SQLITE4_DYNAMIC, 0);
    }
  }
}


#if 0  /* This function is never used. */
/*
** The COALESCE() and IFNULL() functions used to be implemented as shown
** here.  But now they are implemented as VDBE code so that unused arguments
** do not have to be computed.  This legacy implementation is retained as
** comment.
*/
/*
** Implementation of the IFNULL(), NVL(), and COALESCE() functions.  
** All three do the same thing.  They return the first non-NULL
** argument.
*/
static void ifnullFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  int i;
  for(i=0; i<argc; i++){
    if( SQLITE4_NULL!=sqlite4_value_type(argv[i]) ){
      sqlite4_result_value(context, argv[i]);
      break;
    }
  }
}
#endif /* NOT USED */
#define ifnullFunc versionFunc   /* Substitute function - never called */

/*
** Implementation of random().  Return a random integer.  
*/
static void randomFunc(
  sqlite4_context *context,
  int NotUsed,
  sqlite4_value **NotUsed2
){
  sqlite4_int64 r;
  UNUSED_PARAMETER2(NotUsed, NotUsed2);
  sqlite4_randomness(sqlite4_context_env(context), sizeof(r), &r);
  if( r<0 ){
    /* We need to prevent a random number of 0x8000000000000000 
    ** (or -9223372036854775808) since when you do abs() of that
    ** number of you get the same value back again.  To do this
    ** in a way that is testable, mask the sign bit off of negative
    ** values, resulting in a positive value.  Then take the 
    ** 2s complement of that positive value.  The end result can
    ** therefore be no less than -9223372036854775807.
    */
    r = -(r ^ (((sqlite4_int64)1)<<63));
  }
  sqlite4_result_int64(context, r);
}

/*
** Implementation of randomblob(N).  Return a random blob
** that is N bytes long.
*/
static void randomBlob(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  int n;
  unsigned char *p;
  assert( argc==1 );
  UNUSED_PARAMETER(argc);
  n = sqlite4_value_int(argv[0]);
  if( n<1 ){
    n = 1;
  }
  p = contextMalloc(context, n);
  if( p ){
    sqlite4_randomness(sqlite4_context_env(context), n, p);
    sqlite4_result_blob(context, (char*)p, n, SQLITE4_DYNAMIC, 0);
  }
}

/*
** Implementation of the changes() SQL function.
**
** IMP: R-62073-11209 The changes() SQL function is a wrapper
** around the sqlite4_changes() C/C++ function and hence follows the same
** rules for counting changes.
*/
static void changes(
  sqlite4_context *context,
  int NotUsed,
  sqlite4_value **NotUsed2
){
  sqlite4 *db = sqlite4_context_db_handle(context);
  UNUSED_PARAMETER2(NotUsed, NotUsed2);
  sqlite4_result_int(context, sqlite4_changes(db));
}

/*
** Implementation of the total_changes() SQL function.  The return value is
** the same as the sqlite4_total_changes() API function.
*/
static void total_changes(
  sqlite4_context *context,
  int NotUsed,
  sqlite4_value **NotUsed2
){
  sqlite4 *db = sqlite4_context_db_handle(context);
  UNUSED_PARAMETER2(NotUsed, NotUsed2);
  /* IMP: R-52756-41993 This function is a wrapper around the
  ** sqlite4_total_changes() C/C++ interface. */
  sqlite4_result_int(context, sqlite4_total_changes(db));
}

/*
** A structure defining how to do GLOB-style comparisons.
*/
struct compareInfo {
  u8 matchAll;
  u8 matchOne;
  u8 matchSet;
  u8 noCase;
};

/*
** For LIKE and GLOB matching on EBCDIC machines, assume that every
** character is exactly one byte in size.  Also, all characters are
** able to participate in upper-case-to-lower-case mappings in EBCDIC
** whereas only characters less than 0x80 do in ASCII.
*/
#if defined(SQLITE4_EBCDIC)
# define sqlite4Utf8Read(A,C)  (*(A++))
# define GlogUpperToLower(A)   A = sqlite4UpperToLower[A]
#else
# define GlogUpperToLower(A)   if( !((A)&~0x7f) ){ A = sqlite4UpperToLower[A]; }
#endif

static const struct compareInfo globInfo = { '*', '?', '[', 0 };
/* The correct SQL-92 behavior is for the LIKE operator to ignore
** case.  Thus  'a' LIKE 'A' would be true. */
static const struct compareInfo likeInfoNorm = { '%', '_',   0, 1 };
/* If SQLITE4_CASE_SENSITIVE_LIKE is defined, then the LIKE operator
** is case sensitive causing 'a' LIKE 'A' to be false */
static const struct compareInfo likeInfoAlt = { '%', '_',   0, 0 };

/*
** Compare two UTF-8 strings for equality where the first string can
** potentially be a "glob" expression.  Return true (1) if they
** are the same and false (0) if they are different.
**
** Globbing rules:
**
**      '*'       Matches any sequence of zero or more characters.
**
**      '?'       Matches exactly one character.
**
**     [...]      Matches one character from the enclosed list of
**                characters.
**
**     [^...]     Matches one character not in the enclosed list.
**
** With the [...] and [^...] matching, a ']' character can be included
** in the list by making it the first character after '[' or '^'.  A
** range of characters can be specified using '-'.  Example:
** "[a-z]" matches any single lower-case letter.  To match a '-', make
** it the last character in the list.
**
** This routine is usually quick, but can be N**2 in the worst case.
**
** Hints: to match '*' or '?', put them in "[]".  Like this:
**
**         abc[*]xyz        Matches "abc*xyz" only
*/
static int patternCompare(
  const char *zPattern,            /* The glob pattern */
  const char *zString,             /* The string to compare against the glob */
  const struct compareInfo *pInfo, /* Information about how to do the compare */
  u32 esc                          /* The escape character */
){
  u32 c, c2;
  int invert;
  int seen;
  u8 matchOne = pInfo->matchOne;
  u8 matchAll = pInfo->matchAll;
  u8 matchSet = pInfo->matchSet;
  u8 noCase = pInfo->noCase; 
  int prevEscape = 0;     /* True if the previous character was 'escape' */

  while( (c = sqlite4Utf8Read(zPattern,&zPattern))!=0 ){
    if( !prevEscape && c==matchAll ){
      while( (c=sqlite4Utf8Read(zPattern,&zPattern)) == matchAll
               || c == matchOne ){
        if( c==matchOne && sqlite4Utf8Read(zString, &zString)==0 ){
          return 0;
        }
      }
      if( c==0 ){
        return 1;
      }else if( c==esc ){
        c = sqlite4Utf8Read(zPattern, &zPattern);
        if( c==0 ){
          return 0;
        }
      }else if( c==matchSet ){
        assert( esc==0 );         /* This is GLOB, not LIKE */
        assert( matchSet<0x80 );  /* '[' is a single-byte character */
        while( *zString && patternCompare(&zPattern[-1],zString,pInfo,esc)==0 ){
          SQLITE4_SKIP_UTF8(zString);
        }
        return *zString!=0;
      }
      while( (c2 = sqlite4Utf8Read(zString,&zString))!=0 ){
        if( noCase ){
          GlogUpperToLower(c2);
          GlogUpperToLower(c);
          while( c2 != 0 && c2 != c ){
            c2 = sqlite4Utf8Read(zString, &zString);
            GlogUpperToLower(c2);
          }
        }else{
          while( c2 != 0 && c2 != c ){
            c2 = sqlite4Utf8Read(zString, &zString);
          }
        }
        if( c2==0 ) return 0;
        if( patternCompare(zPattern,zString,pInfo,esc) ) return 1;
      }
      return 0;
    }else if( !prevEscape && c==matchOne ){
      if( sqlite4Utf8Read(zString, &zString)==0 ){
        return 0;
      }
    }else if( c==matchSet ){
      u32 prior_c = 0;
      assert( esc==0 );    /* This only occurs for GLOB, not LIKE */
      seen = 0;
      invert = 0;
      c = sqlite4Utf8Read(zString, &zString);
      if( c==0 ) return 0;
      c2 = sqlite4Utf8Read(zPattern, &zPattern);
      if( c2=='^' ){
        invert = 1;
        c2 = sqlite4Utf8Read(zPattern, &zPattern);
      }
      if( c2==']' ){
        if( c==']' ) seen = 1;
        c2 = sqlite4Utf8Read(zPattern, &zPattern);
      }
      while( c2 && c2!=']' ){
        if( c2=='-' && zPattern[0]!=']' && zPattern[0]!=0 && prior_c>0 ){
          c2 = sqlite4Utf8Read(zPattern, &zPattern);
          if( c>=prior_c && c<=c2 ) seen = 1;
          prior_c = 0;
        }else{
          if( c==c2 ){
            seen = 1;
          }
          prior_c = c2;
        }
        c2 = sqlite4Utf8Read(zPattern, &zPattern);
      }
      if( c2==0 || (seen ^ invert)==0 ){
        return 0;
      }
    }else if( esc==c && !prevEscape ){
      prevEscape = 1;
    }else{
      c2 = sqlite4Utf8Read(zString, &zString);
      if( noCase ){
        GlogUpperToLower(c);
        GlogUpperToLower(c2);
      }
      if( c!=c2 ){
        return 0;
      }
      prevEscape = 0;
    }
  }
  return *zString==0;
}

/*
** Count the number of times that the LIKE operator (or GLOB which is
** just a variation of LIKE) gets called.  This is used for testing
** only.
*/
#ifdef SQLITE4_TEST
int sqlite4_like_count = 0;
#endif


/*
** Implementation of the like() SQL function.  This function implements
** the build-in LIKE operator.  The first argument to the function is the
** pattern and the second argument is the string.  So, the SQL statements:
**
**       A LIKE B
**
** is implemented as like(B,A).
**
** This same function (with a different compareInfo structure) computes
** the GLOB operator.
*/
static void likeFunc(
  sqlite4_context *context, 
  int argc, 
  sqlite4_value **argv
){
  const char *zA, *zB;
  u32 escape = 0;
  int nPat;
  sqlite4 *db = sqlite4_context_db_handle(context);

  zB = sqlite4_value_text(argv[0], &nPat);
  zA = sqlite4_value_text(argv[1], 0);

  /* Limit the length of the LIKE or GLOB pattern to avoid problems
  ** of deep recursion and N*N behavior in patternCompare().
  */
  testcase( nPat==db->aLimit[SQLITE4_LIMIT_LIKE_PATTERN_LENGTH] );
  testcase( nPat==db->aLimit[SQLITE4_LIMIT_LIKE_PATTERN_LENGTH]+1 );
  if( nPat > db->aLimit[SQLITE4_LIMIT_LIKE_PATTERN_LENGTH] ){
    sqlite4_result_error(context, "LIKE or GLOB pattern too complex", -1);
    return;
  }

  if( argc==3 ){
    /* The escape character string must consist of a single UTF-8 character.
    ** Otherwise, return an error.
    */
    const char *zEsc = sqlite4_value_text(argv[2], 0);
    if( zEsc==0 ) return;
    if( sqlite4Utf8CharLen(zEsc, -1)!=1 ){
      static const char *zErr = "ESCAPE expression must be a single character";
      sqlite4_result_error(context, zErr, -1);
      return;
    }
    escape = sqlite4Utf8Read(zEsc, &zEsc);
  }
  if( zA && zB ){
    struct compareInfo *pInfo = sqlite4_context_appdata(context);
#ifdef SQLITE4_TEST
    sqlite4_like_count++;
#endif
    
    sqlite4_result_int(context, patternCompare(zB, zA, pInfo, escape));
  }
}

/*
** Implementation of the NULLIF(x,y) function.  The result is the first
** argument if the arguments are different.  The result is NULL if the
** arguments are equal to each other.
*/
static void nullifFunc(
  sqlite4_context *context,
  int NotUsed,
  sqlite4_value **argv
){
  int rc, res;
  CollSeq *pColl = sqlite4GetFuncCollSeq(context);
  UNUSED_PARAMETER(NotUsed);
  rc = sqlite4MemCompare(argv[0], argv[1], pColl, &res);
  if( rc!=SQLITE4_OK ){
    sqlite4_result_error_code(context, rc);
  }else if( res ){
    sqlite4_result_value(context, argv[0]);
  }
}

/*
** Implementation of the sqlite_version() function.  The result is the version
** of the SQLite library that is running.
*/
static void versionFunc(
  sqlite4_context *context,
  int NotUsed,
  sqlite4_value **NotUsed2
){
  UNUSED_PARAMETER2(NotUsed, NotUsed2);
  /* IMP: R-48699-48617 This function is an SQL wrapper around the
  ** sqlite4_libversion() C-interface. */
  sqlite4_result_text(context, sqlite4_libversion(), -1, SQLITE4_STATIC, 0);
}

/*
** Implementation of the sqlite_source_id() function. The result is a string
** that identifies the particular version of the source code used to build
** SQLite.
*/
static void sourceidFunc(
  sqlite4_context *context,
  int NotUsed,
  sqlite4_value **NotUsed2
){
  UNUSED_PARAMETER2(NotUsed, NotUsed2);
  /* IMP: R-24470-31136 This function is an SQL wrapper around the
  ** sqlite4_sourceid() C interface. */
  sqlite4_result_text(context, sqlite4_sourceid(), -1, SQLITE4_STATIC, 0);
}

/*
** Implementation of the sqlite_log() function.  This is a wrapper around
** sqlite4_log().  The return value is NULL.  The function exists purely for
** its side-effects.
*/
static void errlogFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  int rclog = sqlite4_value_int(argv[0]);
  sqlite4_env *pEnv = sqlite4_context_env(context);
  sqlite4_log(pEnv, rclog, "%s", sqlite4_value_text(argv[1], 0));
  UNUSED_PARAMETER(argc);
}

/*
** Implementation of the sqlite_compileoption_used() function.
** The result is an integer that identifies if the compiler option
** was used to build SQLite.
*/
#ifndef SQLITE4_OMIT_COMPILEOPTION_DIAGS
static void compileoptionusedFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  const char *zOptName;
  assert( argc==1 );
  UNUSED_PARAMETER(argc);
  /* IMP: R-39564-36305 The sqlite_compileoption_used() SQL
  ** function is a wrapper around the sqlite4_compileoption_used() C/C++
  ** function.
  */
  if( (zOptName = sqlite4_value_text(argv[0], 0))!=0 ){
    sqlite4_result_int(context, sqlite4_compileoption_used(zOptName));
  }
}
#endif /* SQLITE4_OMIT_COMPILEOPTION_DIAGS */

/*
** Implementation of the sqlite_compileoption_get() function. 
** The result is a string that identifies the compiler options 
** used to build SQLite.
*/
#ifndef SQLITE4_OMIT_COMPILEOPTION_DIAGS
static void compileoptiongetFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  int n;
  assert( argc==1 );
  UNUSED_PARAMETER(argc);
  /* IMP: R-04922-24076 The sqlite_compileoption_get() SQL function
  ** is a wrapper around the sqlite4_compileoption_get() C/C++ function.
  */
  n = sqlite4_value_int(argv[0]);
  sqlite4_result_text(context, sqlite4_compileoption_get(n), -1,
                      SQLITE4_STATIC, 0);
}
#endif /* SQLITE4_OMIT_COMPILEOPTION_DIAGS */

/*
** EXPERIMENTAL - This is not an official function.  The interface may
** change.  This function may disappear.  Do not write code that depends
** on this function.
**
** Implementation of the QUOTE() function.  This function takes a single
** argument.  If the argument is numeric, the return value is the same as
** the argument.  If the argument is NULL, the return value is the string
** "NULL".  Otherwise, the argument is enclosed in single quotes with
** single-quote escapes.
*/
static void quoteFunc(sqlite4_context *context, int argc, sqlite4_value **argv){
  assert( argc==1 );
  UNUSED_PARAMETER(argc);
  switch( sqlite4_value_type(argv[0]) ){
    case SQLITE4_INTEGER:
    case SQLITE4_FLOAT: {
      sqlite4_result_value(context, argv[0]);
      break;
    }
    case SQLITE4_BLOB: {
      int nBlob;
      char *zText = 0;
      const char *zBlob = sqlite4_value_blob(argv[0], &nBlob);
      zText = (char *)contextMalloc(context, (2*(i64)nBlob)+4); 
      if( zText ){
        zText[0] = 'x';
        zText[1] = '\'';
        sqlite4BlobToHex(nBlob, (const u8*)zBlob, zText+2);
        zText[(nBlob*2)+2] = '\'';
        zText[(nBlob*2)+3] = 0;
        sqlite4_result_text(context, zText, -1, SQLITE4_TRANSIENT, 0);
        sqlite4_free(sqlite4_context_env(context), zText);
      }
      break;
    }
    case SQLITE4_TEXT: {
      int i,j;
      u64 n;
      const char *zArg = sqlite4_value_text(argv[0], 0);
      char *z;

      if( zArg==0 ) return;
      for(i=0, n=0; zArg[i]; i++){ if( zArg[i]=='\'' ) n++; }
      z = contextMalloc(context, ((i64)i)+((i64)n)+3);
      if( z ){
        z[0] = '\'';
        for(i=0, j=1; zArg[i]; i++){
          z[j++] = zArg[i];
          if( zArg[i]=='\'' ){
            z[j++] = '\'';
          }
        }
        z[j++] = '\'';
        z[j] = 0;
        sqlite4_result_text(context, z, j, SQLITE4_DYNAMIC, 0);
      }
      break;
    }
    default: {
      assert( sqlite4_value_type(argv[0])==SQLITE4_NULL );
      sqlite4_result_text(context, "NULL", 4, SQLITE4_STATIC, 0);
      break;
    }
  }
}

/*
** The hex() function.  Interpret the argument as a blob.  Return
** a hexadecimal rendering as text.
*/
static void hexFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  int i, n;
  const unsigned char *pBlob;
  char *zHex;
  assert( argc==1 );
  UNUSED_PARAMETER(argc);
  pBlob = sqlite4_value_blob(argv[0], &n);
  zHex = contextMalloc(context, ((i64)n)*2 + 1);
  if( zHex ){
    sqlite4BlobToHex(n, (const u8*)pBlob, zHex);
    sqlite4_result_text(context, zHex, n*2, SQLITE4_DYNAMIC, 0);
  }
}

/*
** The replace() function.  Three arguments are all strings: call
** them A, B, and C. The result is also a string which is derived
** from A by replacing every occurance of B with C.  The match
** must be exact.  Collating sequences are not used.
*/
static void replaceFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  const char *zStr;        /* The input string A */
  const char *zPattern;    /* The pattern string B */
  const char *zRep;        /* The replacement string C */
  char *zOut;              /* The output */
  int nStr;                /* Size of zStr */
  int nPattern;            /* Size of zPattern */
  int nRep;                /* Size of zRep */
  i64 nOut;                /* Maximum size of zOut */
  int loopLimit;           /* Last zStr[] that might match zPattern[] */
  int i, j;                /* Loop counters */

  assert( argc==3 );
  UNUSED_PARAMETER(argc);
  zStr = sqlite4_value_text(argv[0], &nStr);
  if( zStr==0 ) return;
  zPattern = sqlite4_value_text(argv[1], &nPattern);
  if( zPattern==0 ){
    assert( sqlite4_value_type(argv[1])==SQLITE4_NULL
            || sqlite4_context_db_handle(context)->mallocFailed );
    return;
  }
  if( zPattern[0]==0 ){
    assert( sqlite4_value_type(argv[1])!=SQLITE4_NULL );
    sqlite4_result_value(context, argv[0]);
    return;
  }
  zRep = sqlite4_value_text(argv[2], &nRep);
  if( zRep==0 ) return;
  nOut = nStr + 1;
  assert( nOut<SQLITE4_MAX_LENGTH );
  zOut = contextMalloc(context, (i64)nOut);
  if( zOut==0 ){
    return;
  }
  loopLimit = nStr - nPattern;  
  for(i=j=0; i<=loopLimit; i++){
    if( zStr[i]!=zPattern[0] || memcmp(&zStr[i], zPattern, nPattern) ){
      zOut[j++] = zStr[i];
    }else{
      char *zOld;
      sqlite4 *db = sqlite4_context_db_handle(context);
      nOut += nRep - nPattern;
      testcase( nOut-1==db->aLimit[SQLITE4_LIMIT_LENGTH] );
      testcase( nOut-2==db->aLimit[SQLITE4_LIMIT_LENGTH] );
      if( nOut-1>db->aLimit[SQLITE4_LIMIT_LENGTH] ){
        sqlite4_result_error_toobig(context);
        sqlite4_free(db->pEnv, zOut);
        return;
      }
      zOld = zOut;
      zOut = sqlite4_realloc(db->pEnv, zOut, (int)nOut);
      if( zOut==0 ){
        sqlite4_result_error_nomem(context);
        sqlite4_free(db->pEnv, zOld);
        return;
      }
      memcpy(&zOut[j], zRep, nRep);
      j += nRep;
      i += nPattern-1;
    }
  }
  assert( j+nStr-i+1==nOut );
  memcpy(&zOut[j], &zStr[i], nStr-i);
  j += nStr - i;
  assert( j<=nOut );
  zOut[j] = 0;
  sqlite4_result_text(context, zOut, j, SQLITE4_DYNAMIC, 0);
}

/*
** Implementation of the TRIM(), LTRIM(), and RTRIM() functions.
** The userdata is 0x1 for left trim, 0x2 for right trim, 0x3 for both.
*/
static void trimFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  const char *zIn;                /* Input string */
  const char *zCharSet;           /* Set of characters to trim */
  int nIn;                        /* Number of bytes in input */
  int flags;                      /* 1: trimleft  2: trimright  3: trim */
  int i;                          /* Loop counter */
  u8 *aLen = 0;                   /* Length of each character in zCharSet */
  char **azChar = 0;              /* Individual characters in zCharSet */
  int nChar;                      /* Number of characters in zCharSet */

  if( sqlite4_value_type(argv[0])==SQLITE4_NULL ){
    return;
  }
  zIn = sqlite4_value_text(argv[0], &nIn);
  if( zIn==0 ) return;
  if( argc==1 ){
    static const unsigned char lenOne[] = { 1 };
    static char * const azOne[] = { " " };
    nChar = 1;
    aLen = (u8*)lenOne;
    azChar = (char**)azOne;
    zCharSet = 0;
  }else if( (zCharSet = sqlite4_value_text(argv[1], 0))==0 ){
    return;
  }else{
    const char *z;
    for(z=zCharSet, nChar=0; *z; nChar++){
      SQLITE4_SKIP_UTF8(z);
    }
    if( nChar>0 ){
      azChar = contextMalloc(context, ((i64)nChar)*(sizeof(char*)+1));
      if( azChar==0 ){
        return;
      }
      aLen = (unsigned char*)&azChar[nChar];
      for(z=zCharSet, nChar=0; *z; nChar++){
        azChar[nChar] = (char*)z;
        SQLITE4_SKIP_UTF8(z);
        aLen[nChar] = (u8)(z - azChar[nChar]);
      }
    }
  }
  if( nChar>0 ){
    flags = SQLITE4_PTR_TO_INT(sqlite4_context_appdata(context));
    if( flags & 1 ){
      while( nIn>0 ){
        int len = 0;
        for(i=0; i<nChar; i++){
          len = aLen[i];
          if( len<=nIn && memcmp(zIn, azChar[i], len)==0 ) break;
        }
        if( i>=nChar ) break;
        zIn += len;
        nIn -= len;
      }
    }
    if( flags & 2 ){
      while( nIn>0 ){
        int len = 0;
        for(i=0; i<nChar; i++){
          len = aLen[i];
          if( len<=nIn && memcmp(&zIn[nIn-len],azChar[i],len)==0 ) break;
        }
        if( i>=nChar ) break;
        nIn -= len;
      }
    }
    if( zCharSet ){
      sqlite4_free(sqlite4_context_env(context), azChar);
    }
  }
  sqlite4_result_text(context, zIn, nIn, SQLITE4_TRANSIENT, 0);
}


/* IMP: R-25361-16150 This function is omitted from SQLite by default. It
** is only available if the SQLITE4_SOUNDEX compile-time option is used
** when SQLite is built.
*/
#ifdef SQLITE4_SOUNDEX
/*
** Compute the soundex encoding of a word.
**
** IMP: R-59782-00072 The soundex(X) function returns a string that is the
** soundex encoding of the string X. 
*/
static void soundexFunc(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  char zResult[8];
  const u8 *zIn;
  int i, j;
  static const unsigned char iCode[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 2, 3, 0, 1, 2, 0, 0, 2, 2, 4, 5, 5, 0,
    1, 2, 6, 2, 3, 0, 1, 0, 2, 0, 2, 0, 0, 0, 0, 0,
    0, 0, 1, 2, 3, 0, 1, 2, 0, 0, 2, 2, 4, 5, 5, 0,
    1, 2, 6, 2, 3, 0, 1, 0, 2, 0, 2, 0, 0, 0, 0, 0,
  };
  assert( argc==1 );
  zIn = (u8*)sqlite4_value_text(argv[0]);
  if( zIn==0 ) zIn = (u8*)"";
  for(i=0; zIn[i] && !sqlite4Isalpha(zIn[i]); i++){}
  if( zIn[i] ){
    u8 prevcode = iCode[zIn[i]&0x7f];
    zResult[0] = sqlite4Toupper(zIn[i]);
    for(j=1; j<4 && zIn[i]; i++){
      int code = iCode[zIn[i]&0x7f];
      if( code>0 ){
        if( code!=prevcode ){
          prevcode = code;
          zResult[j++] = code + '0';
        }
      }else{
        prevcode = 0;
      }
    }
    while( j<4 ){
      zResult[j++] = '0';
    }
    zResult[j] = 0;
    sqlite4_result_text(context, zResult, 4, SQLITE4_TRANSIENT, 0);
  }else{
    /* IMP: R-64894-50321 The string "?000" is returned if the argument
    ** is NULL or contains no ASCII alphabetic characters. */
    sqlite4_result_text(context, "?000", 4, SQLITE4_STATIC, 0);
  }
}
#endif /* SQLITE4_SOUNDEX */

#if 0 /*ndef SQLITE4_OMIT_LOAD_EXTENSION*/
/*
** A function that loads a shared-library extension then returns NULL.
*/
static void loadExt(sqlite4_context *context, int argc, sqlite4_value **argv){
  const char *zFile = (const char *)sqlite4_value_text(argv[0]);
  const char *zProc;
  sqlite4 *db = sqlite4_context_db_handle(context);
  char *zErrMsg = 0;

  if( argc==2 ){
    zProc = (const char *)sqlite4_value_text(argv[1]);
  }else{
    zProc = 0;
  }
  if( zFile && sqlite4_load_extension(db, zFile, zProc, &zErrMsg) ){
    sqlite4_result_error(context, zErrMsg, -1);
    sqlite4_free(zErrMsg);
  }
}
#endif


/*
** An instance of the following structure holds the context of a
** sum() or avg() aggregate computation.
*/
typedef struct SumCtx SumCtx;
struct SumCtx {
  sqlite4_num sum;  /* Sum so far */
  i64 cnt;          /* Number of elements summed */
  u8 approx;        /* True if non-integer value was input to the sum */
};

/*
** Routines used to compute the sum, average, and total.
**
** The SUM() function follows the (broken) SQL standard which means
** that it returns NULL if it sums over no inputs.  TOTAL returns
** 0.0 in that case.  In addition, TOTAL always returns a float where
** SUM might return an integer if it never encounters a floating point
** value.  TOTAL never fails, but SUM might through an exception if
** it overflows an integer.
*/
static void sumStep(sqlite4_context *context, int argc, sqlite4_value **argv){
  SumCtx *p;
  int type;
  assert( argc==1 );
  UNUSED_PARAMETER(argc);
  p = sqlite4_aggregate_context(context, sizeof(*p));
  type = sqlite4_value_numeric_type(argv[0]);
  if( p && type!=SQLITE4_NULL ){
    p->cnt++;
    p->sum = sqlite4_num_add(p->sum, sqlite4_value_num(argv[0]));
    if( type!=SQLITE4_INTEGER ){
      p->approx = 1;
    }
  }
}
static void sumFinalize(sqlite4_context *context){
  SumCtx *p;
  p = sqlite4_aggregate_context(context, 0);
  if( p && p->cnt>0 ){
    if( p->approx==0 ){
      int bLossy;
      i64 iVal = sqlite4_num_to_int64(p->sum, &bLossy);
      if( bLossy ){
        sqlite4_result_error(context, "integer overflow", -1);
      }else{
        sqlite4_result_int64(context, iVal);
      }
    }else{
      sqlite4_result_num(context, p->sum);
    }
  }
}
static void avgFinalize(sqlite4_context *context){
  SumCtx *p;
  p = sqlite4_aggregate_context(context, 0);
  if( p && p->cnt>0 ){
    sqlite4_result_num(context, 
        sqlite4_num_div(p->sum, sqlite4_num_from_int64(p->cnt))
    );
  }
}
static void totalFinalize(sqlite4_context *context){
  SumCtx *p;
  p = sqlite4_aggregate_context(context, 0);
  sqlite4_result_num(context, (p ? p->sum : sqlite4_num_from_int64(0)));
}

/*
** The following structure keeps track of state information for the
** count() aggregate function.
*/
typedef struct CountCtx CountCtx;
struct CountCtx {
  i64 n;
};

/*
** Routines to implement the count() aggregate function.
*/
static void countStep(sqlite4_context *context, int argc, sqlite4_value **argv){
  CountCtx *p;
  p = sqlite4_aggregate_context(context, sizeof(*p));
  if( (argc==0 || SQLITE4_NULL!=sqlite4_value_type(argv[0])) && p ){
    p->n++;
  }
}   
static void countFinalize(sqlite4_context *context){
  CountCtx *p;
  p = sqlite4_aggregate_context(context, 0);
  sqlite4_result_int64(context, p ? p->n : 0);
}

/*
** Routines to implement min() and max() aggregate functions.
*/
static void minmaxStep(
  sqlite4_context *context, 
  int NotUsed, 
  sqlite4_value **argv
){
  Mem *pArg  = (Mem *)argv[0];
  Mem *pBest;
  UNUSED_PARAMETER(NotUsed);

  if( sqlite4_value_type(argv[0])==SQLITE4_NULL ) return;
  pBest = (Mem *)sqlite4_aggregate_context(context, sizeof(*pBest));
  if( !pBest ) return;

  if( pBest->flags ){
    int max;
    int cmp;
    int rc;
    CollSeq *pColl = sqlite4GetFuncCollSeq(context);
    /* This step function is used for both the min() and max() aggregates,
    ** the only difference between the two being that the sense of the
    ** comparison is inverted. For the max() aggregate, the
    ** sqlite4_context_appdata() function returns (void *)-1. For min() it
    ** returns (void *)db, where db is the sqlite4* database pointer.
    ** Therefore the next statement sets variable 'max' to 1 for the max()
    ** aggregate, or 0 for min().
    */
    max = sqlite4_context_appdata(context)!=0;
    rc = sqlite4MemCompare(pBest, pArg, pColl, &cmp);
    if( rc!=SQLITE4_OK ){
      sqlite4_result_error_code(context, rc);
      return;
    }
    if( (max && cmp<0) || (!max && cmp>0) ){
      sqlite4VdbeMemCopy(pBest, pArg);
    }
  }else{
    sqlite4VdbeMemCopy(pBest, pArg);
  }
}
static void minMaxFinalize(sqlite4_context *context){
  sqlite4_value *pRes;
  pRes = (sqlite4_value *)sqlite4_aggregate_context(context, 0);
  if( pRes ){
    if( ALWAYS(pRes->flags) ){
      sqlite4_result_value(context, pRes);
    }
    sqlite4VdbeMemRelease(pRes);
  }
}

/*
** group_concat(EXPR, ?SEPARATOR?)
*/
static void groupConcatStep(
  sqlite4_context *context,
  int argc,
  sqlite4_value **argv
){
  const char *zVal;
  StrAccum *pAccum;
  const char *zSep;
  int nVal, nSep;
  assert( argc==1 || argc==2 );
  if( sqlite4_value_type(argv[0])==SQLITE4_NULL ) return;
  pAccum = (StrAccum*)sqlite4_aggregate_context(context, sizeof(*pAccum));

  if( pAccum ){
    sqlite4 *db = sqlite4_context_db_handle(context);
    int firstTerm = pAccum->useMalloc==0;
    pAccum->useMalloc = 2;
    pAccum->pEnv = db->pEnv;
    pAccum->mxAlloc = db->aLimit[SQLITE4_LIMIT_LENGTH];
    if( !firstTerm ){
      if( argc==2 ){
        zSep = (char*)sqlite4_value_text(argv[1], &nSep);
      }else{
        zSep = ",";
        nSep = 1;
      }
      sqlite4StrAccumAppend(pAccum, zSep, nSep);
    }
    zVal = (char*)sqlite4_value_text(argv[0], &nVal);
    sqlite4StrAccumAppend(pAccum, zVal, nVal);
  }
}
static void groupConcatFinalize(sqlite4_context *context){
  StrAccum *pAccum;
  pAccum = sqlite4_aggregate_context(context, 0);
  if( pAccum ){
    if( pAccum->tooBig ){
      sqlite4_result_error_toobig(context);
    }else if( pAccum->mallocFailed ){
      sqlite4_result_error_nomem(context);
    }else{    
      sqlite4_result_text(context, sqlite4StrAccumFinish(pAccum), -1, 
                          SQLITE4_DYNAMIC, 0);
    }
  }
}

/*
** This routine does per-connection function registration.  Most
** of the built-in functions above are part of the global function set.
** This routine only deals with those that are not global.
*/
void sqlite4RegisterBuiltinFunctions(sqlite4 *db){
  int rc = sqlite4_overload_function(db, "MATCH", 2);
  assert( rc==SQLITE4_NOMEM || rc==SQLITE4_OK );
  if( rc==SQLITE4_NOMEM ){
    db->mallocFailed = 1;
  }
}

/*
** Set the LIKEOPT flag on the 2-argument function with the given name.
*/
static void setLikeOptFlag(sqlite4 *db, const char *zName, u8 flagVal){
  FuncDef *pDef;
  pDef = sqlite4FindFunction(db, zName, sqlite4Strlen30(zName), 2, 0);
  if( ALWAYS(pDef) ){
    pDef->flags = flagVal;
  }
}

/*
** Register the built-in LIKE and GLOB functions.  The caseSensitive
** parameter determines whether or not the LIKE operator is case
** sensitive.  GLOB is always case sensitive.
*/
void sqlite4RegisterLikeFunctions(sqlite4 *db, int caseSensitive){
  struct compareInfo *pInfo;
  if( caseSensitive ){
    pInfo = (struct compareInfo*)&likeInfoAlt;
  }else{
    pInfo = (struct compareInfo*)&likeInfoNorm;
  }
  sqlite4CreateFunc(db, "like", 2, pInfo, likeFunc, 0, 0, 0);
  sqlite4CreateFunc(db, "like", 3, pInfo, likeFunc, 0, 0, 0);
  sqlite4CreateFunc(db, "glob", 2, 
      (struct compareInfo*)&globInfo, likeFunc, 0, 0, 0);
  setLikeOptFlag(db, "glob", SQLITE4_FUNC_LIKE | SQLITE4_FUNC_CASE);
  setLikeOptFlag(db, "like", 
      caseSensitive ? (SQLITE4_FUNC_LIKE | SQLITE4_FUNC_CASE) : SQLITE4_FUNC_LIKE);
}

/*
** pExpr points to an expression which implements a function.  If
** it is appropriate to apply the LIKE optimization to that function
** then set aWc[0] through aWc[2] to the wildcard characters and
** return TRUE.  If the function is not a LIKE-style function then
** return FALSE.
*/
int sqlite4IsLikeFunction(sqlite4 *db, Expr *pExpr, int *pIsNocase, char *aWc){
  FuncDef *pDef;
  if( pExpr->op!=TK_FUNCTION 
   || !pExpr->x.pList 
   || pExpr->x.pList->nExpr!=2
  ){
    return 0;
  }
  assert( !ExprHasProperty(pExpr, EP_xIsSelect) );
  pDef = sqlite4FindFunction(db, pExpr->u.zToken, 
                             sqlite4Strlen30(pExpr->u.zToken), 2, 0);
  if( NEVER(pDef==0) || (pDef->flags & SQLITE4_FUNC_LIKE)==0 ){
    return 0;
  }

  /* The memcpy() statement assumes that the wildcard characters are
  ** the first three statements in the compareInfo structure.  The
  ** asserts() that follow verify that assumption
  */
  memcpy(aWc, pDef->pUserData, 3);
  assert( (char*)&likeInfoAlt == (char*)&likeInfoAlt.matchAll );
  assert( &((char*)&likeInfoAlt)[1] == (char*)&likeInfoAlt.matchOne );
  assert( &((char*)&likeInfoAlt)[2] == (char*)&likeInfoAlt.matchSet );
  *pIsNocase = (pDef->flags & SQLITE4_FUNC_CASE)==0;
  return 1;
}

/*
** Add all of the FuncDef structures in the aBuiltinFunc[] array above
** to the global function hash table.  This occurs at start-time (as
** a consequence of calling sqlite4_initialize()).
**
** After this routine runs
*/
void sqlite4RegisterGlobalFunctions(sqlite4_env *pEnv){
  /*
  ** The following array holds FuncDef structures for all of the functions
  ** defined in this file.
  **
  ** The array cannot be constant since changes are made to the
  ** FuncDef.pNextName and FuncDef.pSameName elements at start-time.
  */
  static FuncDef aBuiltinFunc[] = {
    FUNCTION(ltrim,              1, 1, 0, trimFunc         ),
    FUNCTION(ltrim,              2, 1, 0, trimFunc         ),
    FUNCTION(rtrim,              1, 2, 0, trimFunc         ),
    FUNCTION(rtrim,              2, 2, 0, trimFunc         ),
    FUNCTION(trim,               1, 3, 0, trimFunc         ),
    FUNCTION(trim,               2, 3, 0, trimFunc         ),
    FUNCTION(min,               -1, 0, 1, minmaxFunc       ),
    FUNCTION(min,                0, 0, 1, 0                ),
    AGGREGATE(min,               1, 0, 1, minmaxStep,      minMaxFinalize ),
    FUNCTION(max,               -1, 1, 1, minmaxFunc       ),
    FUNCTION(max,                0, 1, 1, 0                ),
    AGGREGATE(max,               1, 1, 1, minmaxStep,      minMaxFinalize ),
    FUNCTION(typeof,             1, 0, 0, typeofFunc       ),
    FUNCTION(length,             1, 0, 0, lengthFunc       ),
    FUNCTION(substr,             2, 0, 0, substrFunc       ),
    FUNCTION(substr,             3, 0, 0, substrFunc       ),
    FUNCTION(abs,                1, 0, 0, absFunc          ),
    FUNCTION(round,              1, 0, 0, roundFunc        ),
    FUNCTION(round,              2, 0, 0, roundFunc        ),
    FUNCTION(upper,              1, 0, 0, upperFunc        ),
    FUNCTION(lower,              1, 0, 0, lowerFunc        ),
    FUNCTION(coalesce,           1, 0, 0, 0                ),
    FUNCTION(coalesce,           0, 0, 0, 0                ),
/*  FUNCTION(coalesce,          -1, 0, 0, ifnullFunc       ), */
    {-1,SQLITE4_FUNC_COALESCE,0,0,ifnullFunc,0,0,"coalesce",0,0},
    FUNCTION(hex,                1, 0, 0, hexFunc          ),
/*  FUNCTION(ifnull,             2, 0, 0, ifnullFunc       ), */
    {2,SQLITE4_FUNC_COALESCE,0,0,ifnullFunc,0,0,"ifnull",0,0},
    FUNCTION(random,             0, 0, 0, randomFunc       ),
    FUNCTION(randomblob,         1, 0, 0, randomBlob       ),
    FUNCTION(nullif,             2, 0, 1, nullifFunc       ),
    FUNCTION(sqlite_version,     0, 0, 0, versionFunc      ),
    FUNCTION(sqlite_source_id,   0, 0, 0, sourceidFunc     ),
    FUNCTION(sqlite_log,         2, 0, 0, errlogFunc       ),
#ifndef SQLITE4_OMIT_COMPILEOPTION_DIAGS
    FUNCTION(sqlite_compileoption_used,1, 0, 0, compileoptionusedFunc  ),
    FUNCTION(sqlite_compileoption_get, 1, 0, 0, compileoptiongetFunc  ),
#endif /* SQLITE4_OMIT_COMPILEOPTION_DIAGS */
    FUNCTION(quote,              1, 0, 0, quoteFunc        ),
    FUNCTION(changes,            0, 0, 0, changes          ),
    FUNCTION(total_changes,      0, 0, 0, total_changes    ),
    FUNCTION(replace,            3, 0, 0, replaceFunc      ),
  #ifdef SQLITE4_SOUNDEX
    FUNCTION(soundex,            1, 0, 0, soundexFunc      ),
  #endif
  #if 0 /*ndef SQLITE4_OMIT_LOAD_EXTENSION*/
    FUNCTION(load_extension,     1, 0, 0, loadExt          ),
    FUNCTION(load_extension,     2, 0, 0, loadExt          ),
  #endif
    AGGREGATE(sum,               1, 0, 0, sumStep,         sumFinalize    ),
    AGGREGATE(total,             1, 0, 0, sumStep,         totalFinalize    ),
    AGGREGATE(avg,               1, 0, 0, sumStep,         avgFinalize    ),
 /* AGGREGATE(count,             0, 0, 0, countStep,       countFinalize  ), */
    {0,SQLITE4_FUNC_COUNT,0,0,0,countStep,countFinalize,"count",0,0},
    AGGREGATE(count,             1, 0, 0, countStep,       countFinalize  ),
    AGGREGATE(group_concat,      1, 0, 0, groupConcatStep, groupConcatFinalize),
    AGGREGATE(group_concat,      2, 0, 0, groupConcatStep, groupConcatFinalize),
  
    LIKEFUNC(glob, 2, &globInfo, SQLITE4_FUNC_LIKE|SQLITE4_FUNC_CASE),
  #ifdef SQLITE4_CASE_SENSITIVE_LIKE
    LIKEFUNC(like, 2, &likeInfoAlt, SQLITE4_FUNC_LIKE|SQLITE4_FUNC_CASE),
    LIKEFUNC(like, 3, &likeInfoAlt, SQLITE4_FUNC_LIKE|SQLITE4_FUNC_CASE),
  #else
    LIKEFUNC(like, 2, &likeInfoNorm, SQLITE4_FUNC_LIKE),
    LIKEFUNC(like, 3, &likeInfoNorm, SQLITE4_FUNC_LIKE),
  #endif
  };

  int i;
  FuncDefTable *pFuncTab = &pEnv->aGlobalFuncs;
  FuncDef *aFunc = (FuncDef*)aBuiltinFunc;

  for(i=0; i<ArraySize(aBuiltinFunc); i++){
    sqlite4FuncDefInsert(pFuncTab, &aFunc[i], 1);
  }
  sqlite4RegisterDateTimeFunctions(pEnv);
#ifndef SQLITE4_OMIT_ALTERTABLE
  sqlite4AlterFunctions(pEnv);
#endif
}
