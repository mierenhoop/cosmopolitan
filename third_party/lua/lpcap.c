
#include "third_party/lua/lua.h"
#include "third_party/lua/lauxlib.h"

#include "third_party/lua/lpcap.h"
#include "third_party/lua/lpprint.h"
#include "third_party/lua/lptypes.h"


#define getfromktable(cs,v)	lua_rawgeti((cs)->L, ktableidx((cs)->ptop), v)

#define pushluaval(cs)		getfromktable(cs, (cs)->cap->idx)



#define skipclose(cs,head)  \
	if (isopencap(head)) { assert(isclosecap(cs->cap)); cs->cap++; }


/*
** Return the size of capture 'cap'. If it is an open capture, 'close'
** must be its corresponding close.
*/
static Index_t capsize (Capture *cap, Capture *close) {
  if (isopencap(cap)) {
    assert(isclosecap(close));
    return close->index - cap->index;
  }
  else
    return cap->siz - 1;
}


static Index_t closesize (CapState *cs, Capture *head) {
  return capsize(head, cs->cap);
}


/*
** Put at the cache for Lua values the value indexed by 'v' in ktable
** of the running pattern (if it is not there yet); returns its index.
*/
static int updatecache (CapState *cs, int v) {
  int idx = cs->ptop + 1;  /* stack index of cache for Lua values */
  if (v != cs->valuecached) {  /* not there? */
    getfromktable(cs, v);  /* get value from 'ktable' */
    lua_replace(cs->L, idx);  /* put it at reserved stack position */
    cs->valuecached = v;  /* keep track of what is there */
  }
  return idx;
}


static int pushcapture (CapState *cs);


/*
** Goes back in a list of captures looking for an open capture
** corresponding to a close one.
*/
static Capture *findopen (Capture *cap) {
  int n = 0;  /* number of closes waiting an open */
  for (;;) {
    cap--;
    if (isclosecap(cap)) n++;  /* one more open to skip */
    else if (isopencap(cap))
      if (n-- == 0) return cap;
  }
}


/*
** Go to the next capture at the same level.
*/
static void nextcap (CapState *cs) {
  Capture *cap = cs->cap;
  if (isopencap(cap)) {  /* must look for a close? */
    int n = 0;  /* number of opens waiting a close */
    for (;;) {  /* look for corresponding close */
      cap++;
      if (isopencap(cap)) n++;
      else if (isclosecap(cap))
        if (n-- == 0) break;
    }
    cs->cap = cap + 1;  /* + 1 to skip last close */
  }
  else {
    Capture *next;
    for (next = cap + 1; capinside(cap, next); next++)
      ;  /* skip captures inside current one */
    cs->cap = next;
  }
}


/*
** Push on the Lua stack all values generated by nested captures inside
** the current capture. Returns number of values pushed. 'addextra'
** makes it push the entire match after all captured values. The
** entire match is pushed also if there are no other nested values,
** so the function never returns zero.
*/
static int pushnestedvalues (CapState *cs, int addextra) {
  Capture *head = cs->cap++;  /* original capture */
  int n = 0;  /* number of pushed subvalues */
  /* repeat for all nested patterns */
  while (capinside(head, cs->cap))
    n += pushcapture(cs);
  if (addextra || n == 0) {  /* need extra? */
    lua_pushlstring(cs->L, cs->s + head->index, closesize(cs, head));
    n++;
  }
  skipclose(cs, head);
  return n;
}


/*
** Push only the first value generated by nested captures
*/
static void pushonenestedvalue (CapState *cs) {
  int n = pushnestedvalues(cs, 0);
  if (n > 1)
    lua_pop(cs->L, n - 1);  /* pop extra values */
}


/*
** Checks whether group 'grp' is visible to 'ref', that is, 'grp' is
** not nested inside a full capture that does not contain 'ref'.  (We
** only need to care for full captures because the search at 'findback'
** skips open-end blocks; so, if 'grp' is nested in a non-full capture,
** 'ref' is also inside it.)  To check this, we search backward for the
** inner full capture enclosing 'grp'.  A full capture cannot contain
** non-full captures, so a close capture means we cannot be inside a
** full capture anymore.
*/
static int capvisible (CapState *cs, Capture *grp, Capture *ref) {
  Capture *cap = grp;
  int i = MAXLOP;  /* maximum distance for an 'open' */
  while (i-- > 0 && cap-- > cs->ocap) {
    if (isclosecap(cap))
      return 1;  /* can stop the search */
    else if (grp->index - cap->index >= UCHAR_MAX)
      return 1;  /* can stop the search */
    else if (capinside(cap, grp))  /* is 'grp' inside cap? */
      return capinside(cap, ref);  /* ok iff cap also contains 'ref' */
  }
  return 1;  /* 'grp' is not inside any capture */
}


/*
** Try to find a named group capture with the name given at the top of
** the stack; goes backward from 'ref'.
*/
static Capture *findback (CapState *cs, Capture *ref) {
  lua_State *L = cs->L;
  Capture *cap = ref;
  while (cap-- > cs->ocap) {  /* repeat until end of list */
    if (isclosecap(cap))
      cap = findopen(cap);  /* skip nested captures */
    else if (capinside(cap, ref))
      continue;  /* enclosing captures are not visible to 'ref' */
    if (captype(cap) == Cgroup && capvisible(cs, cap, ref)) {
      getfromktable(cs, cap->idx);  /* get group name */
      if (lp_equal(L, -2, -1)) {  /* right group? */
        lua_pop(L, 2);  /* remove reference name and group name */
        return cap;
      }
      else lua_pop(L, 1);  /* remove group name */
    }
  }
  luaL_error(L, "back reference '%s' not found", lua_tostring(L, -1));
  return NULL;  /* to avoid warnings */
}


/*
** Back-reference capture. Return number of values pushed.
*/
static int backrefcap (CapState *cs) {
  int n;
  Capture *curr = cs->cap;
  pushluaval(cs);  /* reference name */
  cs->cap = findback(cs, curr);  /* find corresponding group */
  n = pushnestedvalues(cs, 0);  /* push group's values */
  cs->cap = curr + 1;
  return n;
}


/*
** Table capture: creates a new table and populates it with nested
** captures.
*/
static int tablecap (CapState *cs) {
  lua_State *L = cs->L;
  Capture *head = cs->cap++;
  int n = 0;
  lua_newtable(L);
  while (capinside(head, cs->cap)) {
    if (captype(cs->cap) == Cgroup && cs->cap->idx != 0) {  /* named group? */
      pushluaval(cs);  /* push group name */
      pushonenestedvalue(cs);
      lua_settable(L, -3);
    }
    else {  /* not a named group */
      int i;
      int k = pushcapture(cs);
      for (i = k; i > 0; i--)  /* store all values into table */
        lua_rawseti(L, -(i + 1), n + i);
      n += k;
    }
  }
  skipclose(cs, head);
  return 1;  /* number of values pushed (only the table) */
}


/*
** Table-query capture
*/
static int querycap (CapState *cs) {
  int idx = cs->cap->idx;
  pushonenestedvalue(cs);  /* get nested capture */
  lua_gettable(cs->L, updatecache(cs, idx));  /* query cap. value at table */
  if (!lua_isnil(cs->L, -1))
    return 1;
  else {  /* no value */
    lua_pop(cs->L, 1);  /* remove nil */
    return 0;
  }
}


/*
** Fold capture
*/
static int foldcap (CapState *cs) {
  int n;
  lua_State *L = cs->L;
  Capture *head = cs->cap++;
  int idx = head->idx;
  if (isclosecap(cs->cap) ||  /* no nested captures (large subject)? */
      (n = pushcapture(cs)) == 0)  /* nested captures with no values? */
    return luaL_error(L, "no initial value for fold capture");
  if (n > 1)
    lua_pop(L, n - 1);  /* leave only one result for accumulator */
  while (capinside(head, cs->cap)) {
    lua_pushvalue(L, updatecache(cs, idx));  /* get folding function */
    lua_insert(L, -2);  /* put it before accumulator */
    n = pushcapture(cs);  /* get next capture's values */
    lua_call(L, n + 1, 1);  /* call folding function */
  }
  skipclose(cs, head);
  return 1;  /* only accumulator left on the stack */
}


/*
** Function capture
*/
static int functioncap (CapState *cs) {
  int n;
  int top = lua_gettop(cs->L);
  pushluaval(cs);  /* push function */
  n = pushnestedvalues(cs, 0);  /* push nested captures */
  lua_call(cs->L, n, LUA_MULTRET);  /* call function */
  return lua_gettop(cs->L) - top;  /* return function's results */
}


/*
** Accumulator capture
*/
static int accumulatorcap (CapState *cs) {
  lua_State *L = cs->L;
  int n;
  if (lua_gettop(L) < cs->firstcap)
    luaL_error(L, "no previous value for accumulator capture");
  pushluaval(cs);  /* push function */
  lua_insert(L, -2);  /* previous value becomes first argument */
  n = pushnestedvalues(cs, 0);  /* push nested captures */
  lua_call(L, n + 1, 1);  /* call function */
  return 0;  /* did not add any extra value */
}


/*
** Select capture
*/
static int numcap (CapState *cs) {
  int idx = cs->cap->idx;  /* value to select */
  if (idx == 0) {  /* no values? */
    nextcap(cs);  /* skip entire capture */
    return 0;  /* no value produced */
  }
  else {
    int n = pushnestedvalues(cs, 0);
    if (n < idx)  /* invalid index? */
      return luaL_error(cs->L, "no capture '%d'", idx);
    else {
      lua_pushvalue(cs->L, -(n - idx + 1));  /* get selected capture */
      lua_replace(cs->L, -(n + 1));  /* put it in place of 1st capture */
      lua_pop(cs->L, n - 1);  /* remove other captures */
      return 1;
    }
  }
}


/*
** Return the stack index of the first runtime capture in the given
** list of captures (or zero if no runtime captures)
*/
int finddyncap (Capture *cap, Capture *last) {
  for (; cap < last; cap++) {
    if (cap->kind == Cruntime)
      return cap->idx;  /* stack position of first capture */
  }
  return 0;  /* no dynamic captures in this segment */
}


/*
** Calls a runtime capture. Returns number of captures "removed" by the
** call, that is, those inside the group capture. Captures to be added
** are on the Lua stack.
*/
int runtimecap (CapState *cs, Capture *close, const char *s, int *rem) {
  int n, id;
  lua_State *L = cs->L;
  int otop = lua_gettop(L);
  Capture *open = findopen(close);  /* get open group capture */
  assert(captype(open) == Cgroup);
  id = finddyncap(open, close);  /* get first dynamic capture argument */
  close->kind = Cclose;  /* closes the group */
  close->index = s - cs->s;
  cs->cap = open; cs->valuecached = 0;  /* prepare capture state */
  luaL_checkstack(L, 4, "too many runtime captures");
  pushluaval(cs);  /* push function to be called */
  lua_pushvalue(L, SUBJIDX);  /* push original subject */
  lua_pushinteger(L, s - cs->s + 1);  /* push current position */
  n = pushnestedvalues(cs, 0);  /* push nested captures */
  lua_call(L, n + 2, LUA_MULTRET);  /* call dynamic function */
  if (id > 0) {  /* are there old dynamic captures to be removed? */
    int i;
    for (i = id; i <= otop; i++)
      lua_remove(L, id);  /* remove old dynamic captures */
    *rem = otop - id + 1;  /* total number of dynamic captures removed */
  }
  else
    *rem = 0;  /* no dynamic captures removed */
  return close - open - 1;  /* number of captures to be removed */
}


/*
** Auxiliary structure for substitution and string captures: keep
** information about nested captures for future use, avoiding to push
** string results into Lua
*/
typedef struct StrAux {
  int isstring;  /* whether capture is a string */
  union {
    Capture *cp;  /* if not a string, respective capture */
    struct {  /* if it is a string... */
      Index_t idx;  /* starts here */
      Index_t siz;  /* with this size */
    } s;
  } u;
} StrAux;

#define MAXSTRCAPS	10

/*
** Collect values from current capture into array 'cps'. Current
** capture must be Cstring (first call) or Csimple (recursive calls).
** (In first call, fills %0 with whole match for Cstring.)
** Returns number of elements in the array that were filled.
*/
static int getstrcaps (CapState *cs, StrAux *cps, int n) {
  int k = n++;
  Capture *head = cs->cap++;
  cps[k].isstring = 1;  /* get string value */
  cps[k].u.s.idx = head->index;  /* starts here */
  while (capinside(head, cs->cap)) {
    if (n >= MAXSTRCAPS)  /* too many captures? */
      nextcap(cs);  /* skip extra captures (will not need them) */
    else if (captype(cs->cap) == Csimple)  /* string? */
      n = getstrcaps(cs, cps, n);  /* put info. into array */
    else {
      cps[n].isstring = 0;  /* not a string */
      cps[n].u.cp = cs->cap;  /* keep original capture */
      nextcap(cs);
      n++;
    }
  }
  cps[k].u.s.siz = closesize(cs, head);
  skipclose(cs, head);
  return n;
}


/*
** add next capture value (which should be a string) to buffer 'b'
*/
static int addonestring (luaL_Buffer *b, CapState *cs, const char *what);


/*
** String capture: add result to buffer 'b' (instead of pushing
** it into the stack)
*/
static void stringcap (luaL_Buffer *b, CapState *cs) {
  StrAux cps[MAXSTRCAPS];
  int n;
  size_t len, i;
  const char *fmt;  /* format string */
  fmt = lua_tolstring(cs->L, updatecache(cs, cs->cap->idx), &len);
  n = getstrcaps(cs, cps, 0) - 1;  /* collect nested captures */
  for (i = 0; i < len; i++) {  /* traverse format string */
    if (fmt[i] != '%')  /* not an escape? */
      luaL_addchar(b, fmt[i]);  /* add it to buffer */
    else if (fmt[++i] < '0' || fmt[i] > '9')  /* not followed by a digit? */
      luaL_addchar(b, fmt[i]);  /* add to buffer */
    else {
      int l = fmt[i] - '0';  /* capture index */
      if (l > n)
        luaL_error(cs->L, "invalid capture index (%d)", l);
      else if (cps[l].isstring)
        luaL_addlstring(b, cs->s + cps[l].u.s.idx, cps[l].u.s.siz);
      else {
        Capture *curr = cs->cap;
        cs->cap = cps[l].u.cp;  /* go back to evaluate that nested capture */
        if (!addonestring(b, cs, "capture"))
          luaL_error(cs->L, "no values in capture index %d", l);
        cs->cap = curr;  /* continue from where it stopped */
      }
    }
  }
}


/*
** Substitution capture: add result to buffer 'b'
*/
static void substcap (luaL_Buffer *b, CapState *cs) {
  const char *curr = cs->s + cs->cap->index;
  Capture *head = cs->cap++;
  while (capinside(head, cs->cap)) {
    Capture *cap = cs->cap;
    const char *caps = cs->s + cap->index;
    luaL_addlstring(b, curr, caps - curr);  /* add text up to capture */
    if (addonestring(b, cs, "replacement"))
      curr = caps + capsize(cap, cs->cap - 1);  /* continue after match */
    else  /* no capture value */
      curr = caps;  /* keep original text in final result */
  }
  /* add last piece of text */
  luaL_addlstring(b, curr, cs->s + head->index + closesize(cs, head) - curr);
  skipclose(cs, head);
}


/*
** Evaluates a capture and adds its first value to buffer 'b'; returns
** whether there was a value
*/
static int addonestring (luaL_Buffer *b, CapState *cs, const char *what) {
  switch (captype(cs->cap)) {
    case Cstring:
      stringcap(b, cs);  /* add capture directly to buffer */
      return 1;
    case Csubst:
      substcap(b, cs);  /* add capture directly to buffer */
      return 1;
    case Cacc:  /* accumulator capture? */
      return luaL_error(cs->L, "invalid context for an accumulator capture");
    default: {
      lua_State *L = cs->L;
      int n = pushcapture(cs);
      if (n > 0) {
        if (n > 1) lua_pop(L, n - 1);  /* only one result */
        if (!lua_isstring(L, -1))
          return luaL_error(L, "invalid %s value (a %s)",
                               what, luaL_typename(L, -1));
        luaL_addvalue(b);
      }
      return n;
    }
  }
}


#if !defined(MAXRECLEVEL)
#define MAXRECLEVEL	200
#endif


/*
** Push all values of the current capture into the stack; returns
** number of values pushed
*/
static int pushcapture (CapState *cs) {
  lua_State *L = cs->L;
  int res;
  luaL_checkstack(L, 4, "too many captures");
  if (cs->reclevel++ > MAXRECLEVEL)
    return luaL_error(L, "subcapture nesting too deep");
  switch (captype(cs->cap)) {
    case Cposition: {
      lua_pushinteger(L, cs->cap->index + 1);
      cs->cap++;
      res = 1;
      break;
    }
    case Cconst: {
      pushluaval(cs);
      cs->cap++;
      res = 1;
      break;
    }
    case Carg: {
      int arg = (cs->cap++)->idx;
      if (arg + FIXEDARGS > cs->ptop)
        return luaL_error(L, "reference to absent extra argument #%d", arg);
      lua_pushvalue(L, arg + FIXEDARGS);
      res = 1;
      break;
    }
    case Csimple: {
      int k = pushnestedvalues(cs, 1);
      lua_insert(L, -k);  /* make whole match be first result */
      res = k;
      break;
    }
    case Cruntime: {
      lua_pushvalue(L, (cs->cap++)->idx);  /* value is in the stack */
      res = 1;
      break;
    }
    case Cstring: {
      luaL_Buffer b;
      luaL_buffinit(L, &b);
      stringcap(&b, cs);
      luaL_pushresult(&b);
      res = 1;
      break;
    }
    case Csubst: {
      luaL_Buffer b;
      luaL_buffinit(L, &b);
      substcap(&b, cs);
      luaL_pushresult(&b);
      res = 1;
      break;
    }
    case Cgroup: {
      if (cs->cap->idx == 0)  /* anonymous group? */
        res = pushnestedvalues(cs, 0);  /* add all nested values */
      else {  /* named group: add no values */
        nextcap(cs);  /* skip capture */
        res = 0;
      }
      break;
    }
    case Cbackref: res = backrefcap(cs); break;
    case Ctable: res = tablecap(cs); break;
    case Cfunction: res = functioncap(cs); break;
    case Cacc: res = accumulatorcap(cs); break;
    case Cnum: res = numcap(cs); break;
    case Cquery: res = querycap(cs); break;
    case Cfold: res = foldcap(cs); break;
    default: assert(0); res = 0;
  }
  cs->reclevel--;
  return res;
}


/*
** Prepare a CapState structure and traverse the entire list of
** captures in the stack pushing its results. 's' is the subject
** string, 'r' is the final position of the match, and 'ptop'
** the index in the stack where some useful values were pushed.
** Returns the number of results pushed. (If the list produces no
** results, push the final position of the match.)
*/
int getcaptures (lua_State *L, const char *s, const char *r, int ptop) {
  Capture *capture = (Capture *)lua_touserdata(L, caplistidx(ptop));
  int n = 0;
  /* printcaplist(capture); */
  if (!isclosecap(capture)) {  /* is there any capture? */
    CapState cs;
    cs.ocap = cs.cap = capture; cs.L = L; cs.reclevel = 0;
    cs.s = s; cs.valuecached = 0; cs.ptop = ptop;
    cs.firstcap = lua_gettop(L) + 1;  /* where first value (if any) will go */
    do {  /* collect their values */
      n += pushcapture(&cs);
    } while (!isclosecap(cs.cap));
    assert(lua_gettop(L) - cs.firstcap == n - 1);
  }
  if (n == 0) {  /* no capture values? */
    lua_pushinteger(L, r - s + 1);  /* return only end position */
    n = 1;
  }
  return n;
}


