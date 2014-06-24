#ifndef LIBALLOCS_H_
#define LIBALLOCS_H_

#ifdef __cplusplus
extern "C" {
typedef bool _Bool;
#endif

#include "addrmap.h"
#include "heap_index.h"

extern void warnx(const char *fmt, ...); // avoid repeating proto
#ifndef NDEBUG
#include <assert.h>
#endif

/* Copied from dumptypes.cpp */
struct uniqtype_cache_word 
{
	unsigned long addr:47;
	unsigned flag:1;
	unsigned bits:16;
};

struct contained {
	signed offset;
	struct uniqtype *ptr;
};

struct uniqtype
{
	struct uniqtype_cache_word cache_word;
	const char *name;
	unsigned short pos_maxoff; // 16 bits
	unsigned short neg_maxoff; // 16 bits
	unsigned nmemb:12;         // 12 bits -- number of `contained's (always 1 if array)
	unsigned is_array:1;       // 1 bit
	unsigned array_len:19;     // 19 bits; 0 means undetermined length
	struct contained contained[]; // there's always at least one of these, even if nmemb == 0
};
#define UNIQTYPE_IS_SUBPROGRAM(u) \
(((u) != (struct uniqtype *) &__uniqtype__void) && \
((u)->pos_maxoff == 0) && \
((u)->neg_maxoff == 0) && !(u)->is_array)

#define MAGIC_LENGTH_POINTER ((1u << 19) - 1u)
#define UNIQTYPE_IS_POINTER_TYPE(u) \
((u)->is_array && (u)->array_len == MAGIC_LENGTH_POINTER)

/* ** begin added for inline get_alloc_info */
#ifndef USE_FAKE_LIBUNWIND
#include <libunwind.h>
#else
#include "fake-libunwind.h"
#endif

#include "addrmap.h"
#include "../src/allocsmt.h"
void init_prefix_tree_from_maps(void);
void prefix_tree_add_missing_maps(void);
enum object_memory_kind prefix_tree_get_memory_kind(const void *obj);
void prefix_tree_print_all_to_stderr(void);
#define debug_printf(lvl, ...) do { \
    if ((lvl) <= __liballocs_debug_level) { \
      warnx( __VA_ARGS__ );  \
    } \
  } while (0)
extern unsigned long __liballocs_aborted_stack;
extern unsigned long __liballocs_aborted_static;
extern unsigned long __liballocs_aborted_unknown_storage;
extern unsigned long __liballocs_hit_heap_case;
extern unsigned long __liballocs_hit_stack_case;
extern unsigned long __liballocs_hit_static_case;
extern unsigned long __liballocs_aborted_unindexed_heap;
extern unsigned long __liballocs_aborted_unrecognised_allocsite;

extern void *main_bp __attribute__((visibility("hidden"))); // beginning of main's stack frame

extern inline struct uniqtype *allocsite_to_uniqtype(const void *allocsite) __attribute__((gnu_inline,always_inline));
extern inline struct uniqtype * __attribute__((gnu_inline)) allocsite_to_uniqtype(const void *allocsite)
{
	if (!allocsite) return NULL;
	assert(__liballocs_allocsmt != NULL);
	struct allocsite_entry **bucketpos = ALLOCSMT_FUN(ADDR, allocsite);
	struct allocsite_entry *bucket = *bucketpos;
	for (struct allocsite_entry *p = bucket; p; p = (struct allocsite_entry *) p->next)
	{
		if (p->allocsite == allocsite)
		{
			return p->uniqtype;
		}
	}
	return NULL;
}

#define maximum_vaddr_range_size (4*1024) // HACK
extern inline struct uniqtype *vaddr_to_uniqtype(const void *vaddr) __attribute__((gnu_inline,always_inline));
extern inline struct uniqtype *__attribute__((gnu_inline)) vaddr_to_uniqtype(const void *vaddr)
{
	assert(__liballocs_allocsmt != NULL);
	if (!vaddr) return NULL;
	struct allocsite_entry **initial_bucketpos = ALLOCSMT_FUN(ADDR, (void*)((intptr_t)vaddr | STACK_BEGIN));
	struct allocsite_entry **bucketpos = initial_bucketpos;
	_Bool might_start_in_lower_bucket = 1;
	do 
	{
		struct allocsite_entry *bucket = *bucketpos;
		for (struct allocsite_entry *p = bucket; p; p = (struct allocsite_entry *) p->next)
		{
			/* NOTE that in this memtable, buckets are sorted by address, so 
			 * we would ideally walk backwards. We can't, so we peek ahead at
			 * p->next. */
			if (p->allocsite <= vaddr && 
				(!p->next || ((struct allocsite_entry *) p->next)->allocsite > vaddr))
			{
				return p->uniqtype;
			}
			might_start_in_lower_bucket &= (p->allocsite > vaddr);
		}
		/* No match? then try the next lower bucket *unless* we've seen 
		 * an object in *this* bucket which starts *before* our target address. 
		 * In that case, no lower-bucket object can span far enough to reach our
		 * static_addr, because to do so would overlap the earlier-starting object. */
		--bucketpos;
	} while (might_start_in_lower_bucket && 
	  (initial_bucketpos - bucketpos) * allocsmt_entry_coverage < maximum_vaddr_range_size);
	return NULL;
}
#undef maximum_vaddr_range_size

#define maximum_static_obj_size (256*1024) // HACK
extern inline struct uniqtype *static_addr_to_uniqtype(const void *static_addr, void **out_object_start) __attribute__((gnu_inline,always_inline));
extern inline struct uniqtype * __attribute__((gnu_inline)) static_addr_to_uniqtype(const void *static_addr, void **out_object_start)
{
	assert(__liballocs_allocsmt != NULL);
	if (!static_addr) return NULL;
	struct allocsite_entry **initial_bucketpos = ALLOCSMT_FUN(ADDR, (void*)((intptr_t)static_addr | (STACK_BEGIN<<1)));
	struct allocsite_entry **bucketpos = initial_bucketpos;
	_Bool might_start_in_lower_bucket = 1;
	do 
	{
		struct allocsite_entry *bucket = *bucketpos;
		for (struct allocsite_entry *p = bucket; p; p = (struct allocsite_entry *) p->next)
		{
			/* NOTE that in this memtable, buckets are sorted by address, so 
			 * we would ideally walk backwards. We can't, so we peek ahead at
			 * p->next. */
			if (p->allocsite <= static_addr && 
				(!p->next || ((struct allocsite_entry *) p->next)->allocsite > static_addr)) 
			{
				if (out_object_start) *out_object_start = p->allocsite;
				return p->uniqtype;
			}
			might_start_in_lower_bucket &= (p->allocsite > static_addr);
		}
		/* No match? then try the next lower bucket *unless* we've seen 
		 * an object in *this* bucket which starts *before* our target address. 
		 * In that case, no lower-bucket object can span far enough to reach our
		 * static_addr, because to do so would overlap the earlier-starting object. */
		--bucketpos;
	} while (might_start_in_lower_bucket && 
	  (initial_bucketpos - bucketpos) * allocsmt_entry_coverage < maximum_static_obj_size);
	return NULL;
}
#undef maximum_vaddr_range_size
extern inline _Bool 
__attribute__((always_inline,gnu_inline))
__liballocs_first_subobject_spanning(
	signed *p_target_offset_within_uniqtype,
	struct uniqtype **p_cur_obj_uniqtype,
	struct uniqtype **p_cur_containing_uniqtype,
	struct contained **p_cur_contained_pos) __attribute__((always_inline,gnu_inline));
/* ** end added for inline get_alloc_info */

extern int __liballocs_debug_level;
extern _Bool __liballocs_is_initialized __attribute__((weak));

int __liballocs_global_init(void) __attribute__((weak));
// declare as const void *-returning, to simplify trumptr
const void *__liballocs_typestr_to_uniqtype(const char *typestr) __attribute__((weak));
void *__liballocs_my_typeobj(void) __attribute__((weak));

/* Uniqtypes for signed_char and unsigned_char -- we declare them as int 
 * to avoid the need to define struct uniqtype in this header file. */

extern struct uniqtype __uniqtype__signed_char __attribute__((weak));
extern struct uniqtype __uniqtype__unsigned_char __attribute__((weak));
extern struct uniqtype __uniqtype__void __attribute__((weak));
extern struct uniqtype __uniqtype__int __attribute__((weak));

#define DEFAULT_ATTRS __attribute__((visibility("protected")))

/* Iterate over all uniqtypes in a given shared object. */
int __liballocs_iterate_types(void *typelib_handle, 
		int (*cb)(struct uniqtype *t, void *arg), void *arg) DEFAULT_ATTRS;
/* Our main API: query allocation information for a pointer */
extern inline _Bool __liballocs_get_alloc_info(const void *obj, const void *test_uniqtype, 
	const char **out_reason, const void **out_reason_ptr,
	memory_kind *out_memory_kind, const void **out_object_start,
	unsigned *out_block_element_count,
	struct uniqtype **out_alloc_uniqtype, const void **out_alloc_site,
	signed *out_target_offset_within_uniqtype) DEFAULT_ATTRS __attribute__((gnu_inline,hot));
extern inline _Bool __liballocs_find_matching_subobject(signed target_offset_within_uniqtype,
	struct uniqtype *cur_obj_uniqtype, struct uniqtype *test_uniqtype, 
	struct uniqtype **last_attempted_uniqtype, signed *last_uniqtype_offset,
		signed *p_cumulative_offset_searched) DEFAULT_ATTRS __attribute__((gnu_inline,hot));;
/* Some inlines follow at the bottom. */

/* our own private assert */
extern inline void
__attribute__((always_inline,gnu_inline))
__liballocs_private_assert (_Bool cond, const char *reason, 
	const char *f, unsigned l, const char *fn)
{
#ifndef NDEBUG
	if (!cond) __assert_fail(reason, f, l, fn);
#endif
}

extern inline void 
__attribute__((always_inline,gnu_inline))
__liballocs_ensure_init(void)
{
	//__liballocs_private_assert(__liballocs_check_init() == 0, "liballocs init", 
	//	__FILE__, __LINE__, __func__);
	if (__builtin_expect(! & __liballocs_is_initialized, 0))
	{
		/* This means that we're not linked with libcrunch. 
		 * There's nothing we can do! */
		__liballocs_private_assert(0, "liballocs presence", 
			__FILE__, __LINE__, __func__);
	}
	if (__builtin_expect(!__liballocs_is_initialized, 0))
	{
		/* This means we haven't initialized.
		 * Try that now (it won't try more than once). */
		int ret = __liballocs_global_init();
		__liballocs_private_assert(ret == 0, "liballocs init", 
			__FILE__, __LINE__, __func__);
	}
}

extern inline _Bool 
__liballocs_first_subobject_spanning(
	signed *p_target_offset_within_uniqtype,
	struct uniqtype **p_cur_obj_uniqtype,
	struct uniqtype **p_cur_containing_uniqtype,
	struct contained **p_cur_contained_pos) __attribute__((always_inline,gnu_inline));

extern inline _Bool 
__attribute__((always_inline,gnu_inline))
__liballocs_first_subobject_spanning(
	signed *p_target_offset_within_uniqtype,
	struct uniqtype **p_cur_obj_uniqtype,
	struct uniqtype **p_cur_containing_uniqtype,
	struct contained **p_cur_contained_pos)
{
	struct uniqtype *cur_obj_uniqtype = *p_cur_obj_uniqtype;
	signed target_offset_within_uniqtype = *p_target_offset_within_uniqtype;
	/* Calculate the offset to descend to, if any. This is different for 
	 * structs versus arrays. */
	if (cur_obj_uniqtype->is_array)
	{
		unsigned num_contained = cur_obj_uniqtype->array_len;
		struct uniqtype *element_uniqtype = cur_obj_uniqtype->contained[0].ptr;
		unsigned target_element_index
		 = target_offset_within_uniqtype / element_uniqtype->pos_maxoff;
		if (num_contained > target_element_index)
		{
			*p_cur_containing_uniqtype = cur_obj_uniqtype;
			*p_cur_contained_pos = &cur_obj_uniqtype->contained[0];
			*p_cur_obj_uniqtype = element_uniqtype;
			*p_target_offset_within_uniqtype = target_offset_within_uniqtype % element_uniqtype->pos_maxoff;
			return 1;
		} else return 0;
	}
	else // struct/union case
	{
		unsigned num_contained = cur_obj_uniqtype->nmemb;

		int lower_ind = 0;
		int upper_ind = num_contained;
		while (lower_ind + 1 < upper_ind) // difference of >= 2
		{
			/* Bisect the interval */
			int bisect_ind = (upper_ind + lower_ind) / 2;
			__liballocs_private_assert(bisect_ind > lower_ind, "bisection progress", 
				__FILE__, __LINE__, __func__);
			if (cur_obj_uniqtype->contained[bisect_ind].offset > target_offset_within_uniqtype)
			{
				/* Our solution lies in the lower half of the interval */
				upper_ind = bisect_ind;
			} else lower_ind = bisect_ind;
		}

		if (lower_ind + 1 == upper_ind)
		{
			/* We found one offset */
			__liballocs_private_assert(cur_obj_uniqtype->contained[lower_ind].offset <= target_offset_within_uniqtype,
				"offset underapproximates", __FILE__, __LINE__, __func__);

			/* ... but we might not have found the *lowest* index, in the 
			 * case of a union. Scan backwards so that we have the lowest. 
			 * FIXME: need to account for the element size? Or here are we
			 * ignoring padding anyway? */
			while (lower_ind > 0 
				&& cur_obj_uniqtype->contained[lower_ind-1].offset
					 == cur_obj_uniqtype->contained[lower_ind].offset)
			{
				--lower_ind;
			}
			*p_cur_contained_pos = &cur_obj_uniqtype->contained[lower_ind];
			*p_cur_containing_uniqtype = cur_obj_uniqtype;
			*p_cur_obj_uniqtype
			 = cur_obj_uniqtype->contained[lower_ind].ptr;
			/* p_cur_obj_uniqtype now points to the subobject's uniqtype. 
			 * We still have to adjust the offset. */
			*p_target_offset_within_uniqtype
			 = target_offset_within_uniqtype - cur_obj_uniqtype->contained[lower_ind].offset;

			return 1;
		}
		else /* lower_ind >= upper_ind */
		{
			// this should mean num_contained == 0
			__liballocs_private_assert(num_contained == 0,
				"no contained objects", __FILE__, __LINE__, __func__);
			return 0;
		}
	}
}

extern inline
_Bool 
__attribute__((gnu_inline))
__liballocs_find_matching_subobject(signed target_offset_within_uniqtype,
	struct uniqtype *cur_obj_uniqtype, struct uniqtype *test_uniqtype, 
	struct uniqtype **last_attempted_uniqtype, signed *last_uniqtype_offset,
		signed *p_cumulative_offset_searched) __attribute__((gnu_inline));

extern inline
_Bool 
__attribute__((gnu_inline))
__liballocs_find_matching_subobject(signed target_offset_within_uniqtype,
	struct uniqtype *cur_obj_uniqtype, struct uniqtype *test_uniqtype, 
	struct uniqtype **last_attempted_uniqtype, signed *last_uniqtype_offset,
		signed *p_cumulative_offset_searched)
{
	if (target_offset_within_uniqtype == 0 && (!test_uniqtype || cur_obj_uniqtype == test_uniqtype)) return 1;
	else
	{
		/* We might have *multiple* subobjects spanning the offset. 
		 * Test all of them. */
		struct uniqtype *containing_uniqtype = NULL;
		struct contained *contained_pos = NULL;
		
		signed sub_target_offset = target_offset_within_uniqtype;
		struct uniqtype *contained_uniqtype = cur_obj_uniqtype;
		
		_Bool success = __liballocs_first_subobject_spanning(
			&sub_target_offset, &contained_uniqtype,
			&containing_uniqtype, &contained_pos);
		// now we have a *new* sub_target_offset and contained_uniqtype
		
		if (!success) return 0;
		
		*p_cumulative_offset_searched += contained_pos->offset;
		
		if (last_attempted_uniqtype) *last_attempted_uniqtype = contained_uniqtype;
		if (last_uniqtype_offset) *last_uniqtype_offset = sub_target_offset;
		do {
			assert(containing_uniqtype == cur_obj_uniqtype);
			_Bool recursive_test = __liballocs_find_matching_subobject(
					sub_target_offset,
					contained_uniqtype, test_uniqtype, 
					last_attempted_uniqtype, last_uniqtype_offset, p_cumulative_offset_searched);
			if (__builtin_expect(recursive_test, 1)) return 1;
			// else look for a later contained subobject at the same offset
			unsigned subobj_ind = contained_pos - &containing_uniqtype->contained[0];
			assert(subobj_ind >= 0);
			assert(subobj_ind == 0 || subobj_ind < containing_uniqtype->nmemb);
			if (__builtin_expect(
					containing_uniqtype->nmemb <= subobj_ind + 1
					|| containing_uniqtype->contained[subobj_ind + 1].offset != 
						containing_uniqtype->contained[subobj_ind].offset,
				1))
			{
				// no more subobjects at the same offset, so fail
				return 0;
			} 
			else
			{
				contained_pos = &containing_uniqtype->contained[subobj_ind + 1];
				contained_uniqtype = contained_pos->ptr;
			}
		} while (1);
		
		assert(0);
	}
}

extern inline 
_Bool 
__attribute__((always_inline,gnu_inline)) 
__liballocs_get_alloc_info
	(const void *obj, 
	const void *test_uniqtype, 
	const char **out_reason,
	const void **out_reason_ptr,
	memory_kind *out_memory_kind,
	const void **out_object_start,
	unsigned *out_block_element_count,
	struct uniqtype **out_alloc_uniqtype, 
	const void **out_alloc_site,
	signed *out_target_offset_within_uniqtype) __attribute__((always_inline,gnu_inline));

extern inline 
_Bool 
__attribute__((always_inline,gnu_inline)) 
__liballocs_get_alloc_info
	(const void *obj, 
	const void *test_uniqtype, 
	const char **out_reason,
	const void **out_reason_ptr,
	memory_kind *out_memory_kind,
	const void **out_object_start,
	unsigned *out_block_element_count,
	struct uniqtype **out_alloc_uniqtype, 
	const void **out_alloc_site,
	signed *out_target_offset_within_uniqtype)
{
	int modulo; 
	signed target_offset_wholeblock;
	signed target_offset_within_uniqtype;

	memory_kind k = get_object_memory_kind(obj);
	if (__builtin_expect(k == UNKNOWN, 0))
	{
		k = prefix_tree_get_memory_kind(obj);
		if (__builtin_expect(k == UNKNOWN, 0))
		{
			// still unknown? we have one last trick, if not blacklisted
			_Bool blacklisted = 0;//check_blacklist(obj);
			if (!blacklisted)
			{
				prefix_tree_add_missing_maps();
				k = prefix_tree_get_memory_kind(obj);
				if (k == UNKNOWN)
				{
					prefix_tree_print_all_to_stderr();
					// completely wild pointer or kernel pointer
					debug_printf(1, "liballocs saw wild pointer %p from caller %p\n", obj,
						__builtin_return_address(0));
					//consider_blacklisting(obj);
				}
			}
		}
	}
	*out_alloc_site = 0; // will likely get updated later
	*out_memory_kind = k;
	switch(k)
	{
		case STACK:
		{
			++__liballocs_hit_stack_case;
#define BEGINNING_OF_STACK (STACK_BEGIN - 1)
			// we want to walk a sequence of vaddrs!
			// how do we know which is the one we want?
			// we can get a uniqtype for each one, including maximum posoff and negoff
			// -- yes, use those
			// begin pasted-then-edited from stack.cpp in pmirror
			/* We declare all our variables up front, in the hope that we can rely on
			 * the stack pointer not moving between getcontext and the sanity check.
			 * FIXME: better would be to write this function in C90 and compile with
			 * special flags. */
			unw_cursor_t cursor, saved_cursor, prev_saved_cursor;
			unw_word_t higherframe_sp = 0, sp, higherframe_bp = 0, bp = 0, ip = 0, higherframe_ip = 0, callee_ip;
			int unw_ret;
			unw_context_t unw_context;

			unw_ret = unw_getcontext(&unw_context);
			unw_init_local(&cursor, /*this->unw_as,*/ &unw_context);

			unw_ret = unw_get_reg(&cursor, UNW_REG_SP, &higherframe_sp);
#ifndef NDEBUG
			unw_word_t check_higherframe_sp;
			// sanity check
#ifdef UNW_TARGET_X86
			__asm__ ("movl %%esp, %0\n" :"=r"(check_higherframe_sp));
#else // assume X86_64 for now
			__asm__("movq %%rsp, %0\n" : "=r"(check_higherframe_sp));
#endif
			assert(check_higherframe_sp == higherframe_sp);
#endif
			unw_ret = unw_get_reg(&cursor, UNW_REG_IP, &higherframe_ip);

			int step_ret;
			_Bool at_or_above_main = 0;
			do
			{
				callee_ip = ip;
				prev_saved_cursor = saved_cursor;	// prev_saved_cursor is the cursor into the callee's frame 
													// FIXME: will be garbage if callee_ip == 0
				saved_cursor = cursor; // saved_cursor is the *current* frame's cursor
					// and cursor, later, becomes the *next* (i.e. caller) frame's cursor

				/* First get the ip, sp and symname of the current stack frame. */
				unw_ret = unw_get_reg(&cursor, UNW_REG_IP, &ip); assert(unw_ret == 0);
				unw_ret = unw_get_reg(&cursor, UNW_REG_SP, &sp); assert(unw_ret == 0); // sp = higherframe_sp
				// try to get the bp, but no problem if we don't
				unw_ret = unw_get_reg(&cursor, UNW_TDEP_BP, &bp); 
				_Bool got_bp = (unw_ret == 0);
				_Bool got_higherframe_bp = 0;
				/* Also do a test about whether we're in main, above which we want to
				 * tolerate unwind failures more gracefully.
				 */
				char proc_name_buf[100];
				unw_word_t byte_offset_from_proc_start;
				at_or_above_main |= 
					(
						(got_bp && bp >= (intptr_t) main_bp)
					 || (sp >= (intptr_t) main_bp) // NOTE: this misses the in-main case
					);

				/* Now get the sp of the next higher stack frame, 
				 * i.e. the bp of the current frame. NOTE: we're still
				 * processing the stack frame ending at sp, but we
				 * hoist the unw_step call to here so that we can get
				 * the *bp* of the current frame a.k.a. the caller's bp 
				 * (without demanding that libunwind provides bp, e.g. 
				 * for code compiled with -fomit-frame-pointer). 
				 * This means "cursor" is no longer current -- use 
				 * saved_cursor for the remainder of this iteration!
				 * saved_cursor points to the deeper stack frame. */
				int step_ret = unw_step(&cursor);
				if (step_ret > 0)
				{
					unw_ret = unw_get_reg(&cursor, UNW_REG_SP, &higherframe_sp); assert(unw_ret == 0);
					// assert that for non-top-end frames, BP --> saved-SP relation holds
					// FIXME: hard-codes calling convention info
					if (got_bp && !at_or_above_main && higherframe_sp != bp + 2 * sizeof (void*))
					{
						// debug_printf(2, "Saw frame boundary with unusual sp/bp relation (higherframe_sp=%p, bp=%p != higherframe_sp + 2*sizeof(void*))", 
						// 	higherframe_sp, bp);
					}
					unw_ret = unw_get_reg(&cursor, UNW_REG_IP, &higherframe_ip); assert(unw_ret == 0);
					// try to get the bp, but no problem if we don't
					unw_ret = unw_get_reg(&cursor, UNW_TDEP_BP, &higherframe_bp); 
					got_higherframe_bp = (unw_ret == 0) && higherframe_bp != 0;
				}
				/* NOTE that -UNW_EBADREG happens near the top of the stack where 
				 * unwind info gets patchy, so we should handle it mostly like the 
				 * BEGINNING_OF_STACK case if so... but only if we're at or above main
				 * (and anyway, we *should* have that unwind info, damnit!).
				 */
				else if (step_ret == 0 || (at_or_above_main && step_ret == -UNW_EBADREG))
				{
					higherframe_sp = BEGINNING_OF_STACK;
					higherframe_bp = BEGINNING_OF_STACK;
					got_higherframe_bp = 1;
					higherframe_ip = 0x0;
				}
				else
				{
					// return value <1 means error

					*out_reason = "stack walk step failure";
					goto abort_stack;
					break;
				}
				
				// useful variables at this point: sp, ip, got_bp && bp, 
				// higherframe_sp, higherframe_ip, 
				// callee_ip

				// now do the stuff
				
				/* NOTE: here we are doing one vaddr_to_uniqtype per frame.
				 * Can we optimise this, by ruling out some frames just by
				 * their bounding sps? YES, I'm sure we can. FIXME: do this!
				 * The difficulty is in the fact that frame offsets can be
				 * negative, i.e. arguments exist somewhere in the parent
				 * frame. */
				// 0. if our target address is greater than higherframe_bp, continue
				if (got_higherframe_bp && (uintptr_t) obj > higherframe_bp)
				{
					continue;
				}
				
				// (if our target address is *lower* than sp, we'll abandon the walk, below)
				
				// 1. get the frame uniqtype for frame_ip
				struct uniqtype *frame_desc = vaddr_to_uniqtype((void *) ip);
				if (!frame_desc)
				{
					// no frame descriptor for this frame; that's okay!
					// e.g. our liballocs frames should (normally) have no descriptor
					continue;
				}
				// 2. what's the frame base? it's the higherframe stack pointer
				unsigned char *frame_base = (unsigned char *) higherframe_sp;
				// 3. is our candidate addr between frame-base - negoff and frame_base + posoff?
				if ((unsigned char *) obj >= frame_base - frame_desc->neg_maxoff  // is unsigned, so subtract
					&& (unsigned char *) obj < frame_base + frame_desc->pos_maxoff)
				{
					*out_object_start = frame_base;
					*out_alloc_uniqtype = frame_desc;
					*out_alloc_site = (void*)(intptr_t) ip; // HMM -- is this the best way to represent this?
					goto out_success;
				}
				else
				{
					// have we gone too far? we are going upwards in memory...
					// ... so if our lowest addr is still too high
					if (frame_base - frame_desc->neg_maxoff > (unsigned char *) obj)
					{
						*out_reason = "stack walk reached higher frame";
						goto abort_stack;
					}
				}

				assert(step_ret > 0 || higherframe_sp == BEGINNING_OF_STACK);
			} while (higherframe_sp != BEGINNING_OF_STACK);
			// if we hit the termination condition, we've failed
			if (higherframe_sp == BEGINNING_OF_STACK)
			{
				*out_reason = "stack walk reached top-of-stack";
				goto abort_stack;
			}
		#undef BEGINNING_OF_STACK
		// end pasted from pmirror stack.cpp
		break; // end case STACK
		abort_stack:
			if (!*out_reason) *out_reason = "stack object";
			*out_reason_ptr = obj;
			++__liballocs_aborted_stack;
			return 1;
		} // end case STACK
		case HEAP:
		{
			++__liballocs_hit_heap_case;
			/* For heap allocations, we look up the allocation site.
			 * (This also yields an offset within a toplevel object.)
			 * Then we translate the allocation site to a uniqtypes rec location.
			 * (For direct calls in eagerly-loaded code, we can cache this information
			 * within uniqtypes itself. How? Make uniqtypes include a hash table with
			 * initial contents mapping allocsites to uniqtype recs. This hash table
			 * is initialized during load, but can be extended as new allocsites
			 * are discovered, e.g. indirect ones.)
			 */
			struct suballocated_chunk_rec *containing_suballoc = NULL;
			size_t alloc_chunksize;
			struct insert *heap_info = lookup_object_info(obj, (void**) out_object_start, 
					&alloc_chunksize, &containing_suballoc);
			if (!heap_info)
			{
				*out_reason = "unindexed heap object";
				*out_reason_ptr = obj;
				++__liballocs_aborted_unindexed_heap;
				return 1;
			}
			assert(get_object_memory_kind(heap_info) == HEAP
				|| get_object_memory_kind(heap_info) == UNKNOWN); // might not have seen that maps yet
			assert(
				prefix_tree_get_memory_kind((void*)(uintptr_t) heap_info->alloc_site) == STATIC
				|| (prefix_tree_add_missing_maps(),
					 prefix_tree_get_memory_kind((void*)(uintptr_t) heap_info->alloc_site) == STATIC));

			/* Now we have a uniqtype or an allocsite. For long-lived objects 
			 * the uniqtype will have been installed in the heap header already.
			 * This is the expected case.
			 */
			struct uniqtype *alloc_uniqtype;
			if (__builtin_expect(heap_info->alloc_site_flag, 1))
			{
				if (out_alloc_site) *out_alloc_site = NULL;
				alloc_uniqtype = (void*)(uintptr_t)heap_info->alloc_site;
			}
			else
			{
				/* Look up the allocsite's uniqtype, and install it in the heap info 
				 * (on NDEBUG builds only, because it reduces debuggability a bit). */
				void *alloc_site = (void*)(uintptr_t)heap_info->alloc_site;
				if (out_alloc_site) *out_alloc_site = alloc_site;
				alloc_uniqtype = allocsite_to_uniqtype(alloc_site/*, heap_info*/);
#ifdef NDEBUG
				// install it for future lookups
				// FIXME: make this atomic using a union
				heap_info->alloc_site_flag = 1;
				heap_info->alloc_site = (uintptr_t) alloc_uniqtype;
#endif
			}
			
			// if we didn't get an alloc uniqtype, we abort
			if (!alloc_uniqtype) 
			{
				*out_reason = "unrecognised allocsite";
				*out_reason_ptr = out_alloc_site ? *out_alloc_site : NULL;
				++__liballocs_aborted_unrecognised_allocsite;
				return 1;
			}
			
			// else do the other outputs
			if (out_alloc_uniqtype) *out_alloc_uniqtype = alloc_uniqtype;
			if (out_block_element_count)
			{
				if (alloc_uniqtype) 
				{
					*out_block_element_count = (alloc_chunksize - sizeof (struct insert)) / alloc_uniqtype->pos_maxoff;
				} else *out_block_element_count = 1;
			}
			break;
		}
		case STATIC:
		{
			++__liballocs_hit_static_case;
//			/* We use a blacklist to rule out static addrs that map to things like 
//			 * mmap()'d regions (which we never have typeinfo for)
//			 * or uninstrumented libraries (which we happen not to have typeinfo for). */
//			_Bool blacklisted = check_blacklist(obj);
//			if (blacklisted)
//			{
//				// FIXME: record blacklist hits separately
//				reason = "unrecognised static object";
//				reason_ptr = obj;
//				++__liballocs_aborted_static;
//				goto abort;
//			}
			*out_alloc_uniqtype = static_addr_to_uniqtype(obj, (void**) out_object_start);
			if (!*out_alloc_uniqtype)
			{
				*out_reason = "unrecognised static object";
				*out_reason_ptr = obj;
				++__liballocs_aborted_static;
//				consider_blacklisting(obj);
				return 1;
			}
			// else we can go ahead
			*out_alloc_site = *out_object_start;
			break;
		}
		case UNKNOWN:
		case MAPPED_FILE:
		default:
		{
			*out_reason = "object of unknown storage";
			*out_reason_ptr = obj;
			++__liballocs_aborted_unknown_storage;
			return 1;
		}
	}
	
out_success:
	target_offset_wholeblock = (char*) obj - (char*) *out_object_start;
	/* If we're searching in a heap array, we need to take the offset modulo the 
	 * element size. Otherwise just take the whole-block offset. */
	if (k == HEAP 
			&& out_alloc_uniqtype
			&& (*out_alloc_uniqtype)->pos_maxoff != 0 
			&& (*out_alloc_uniqtype)->neg_maxoff == 0)
	{
		target_offset_within_uniqtype = target_offset_wholeblock % (*out_alloc_uniqtype)->pos_maxoff;
	} else target_offset_within_uniqtype = target_offset_wholeblock;
	// assert that the signs are the same
	assert(target_offset_wholeblock < 0 
		? target_offset_within_uniqtype < 0 
		: target_offset_within_uniqtype >= 0);
	*out_target_offset_within_uniqtype = target_offset_within_uniqtype;
	
	return 0;
}


#ifdef __cplusplus
} // end extern "C"
#endif

#endif