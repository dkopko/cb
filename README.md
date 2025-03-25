## What is CB?

CB is an experiment toward a language runtime which targets at least the 
following features:
* Wait-free Garbage Collection (GC)
* M:N threading
* Value-based semantics
* Persistent data structures

It is hoped that this runtime will ultimately:
* Be an extremely performant M:N runtime for future language syntax(es)
* Allow use of Garbage Collection even in low-latency applications
* Offer a simple and elegant conceptual framework of implementation
* Eliminate distinctions between stack vs. dynamic (heap) allocation


## Background

* Minimally, a Garbage Collector reclaims no-longer-used areas of memory
  for use by future allocations.  Simply reclaiming these areas of memory where 
they exist permits increasing fragmentation over time.  Fragmentation is 
essentially small areas of available memory interspersed between allocated 
areas such that the total quantity of available memory exceeds a requested 
allocation, but there doesn't exist a contiguous amount of available memory 
sufficient to fulfill the requested allocation.  To avoid such undesirable 
fragmentation, a Garbage Collector may also perform consolidation of the 
no-longer-used memory in a phase known as "compaction".
* Compaction must make the no-longer-used areas of memory contiguous with one 
  another.  This implies that the still-in-use areas of memory will also become 
contiguous with one another.  This implies that some still-in-use areas of 
memory must be relocated from where they have existed in memory to some new 
location.
* Relocation of in-use areas of memory is problematic for program state 
  directly holding addresses of old locations (e.g. Java references and C/C++ 
pointers).  Typically as part of the garbage collection all such references 
need to be rewritten to point to the new consolidated locations.  While these 
references are being rewritten to point to their new locations the Mutator 
thread(s) must pause, which is what causes garbage collection pauses.
* RAM is essentially an `address -> value` O(1) mapping.  In the model of 
  C/C++, every byte of RAM is directly referencable/addressable via a `char*`, 
but this is not necessary for a programming language.  The very successful Java 
language (among others) has shown it is sufficient to have references which can 
only refer to class instances, without providing pointers that can refer to any 
byte.
*  Under the covers, Java references are implemented as containing an address 
   whose valid values are a much sparser set than C/C++ (not being allowed to 
point to just any byte, but rather just to the start byte of instances). The
address is an implementation detail and is not exposed to the programmer.  An 
alternate implementation of references could instead hold a non-address
"handle" as long as there were a way for the VM to ultimately translate this 
handle into the needed address of the referred-to structure.  This requires a 
lookup by the VM of `handle -> address` whenever a reference would be be 
dereferenced.  Having this layer of abstraction would allow the Compaction 
phase to rewrite only this lookup table instead of every reference's contained 
address throughout all of the program state.


## Sketch of Wait-free GC Mechanism

* The program thread (a.k.a. "Mutator") to hold 3 maps of `handle -> address`: 
  A, B, C.
* Mutator only ever modifies A.
* These maps have a precedence: entries in A supersede entries in B which 
  supersede entries in C.  (It can be said that A "overlays" B "overlays" C.)
* When Mutator needs to lookup an address for a given handle it checks A then B 
  then C, stopping when it finds the entry.
* Deletions of an entry for handle `h` must be performed by storing a mapping 
  `h -> TOMBSTONE` in A, because a simple deletion of `h` in A would otherwise 
allow an entry for `h` to "peek through" from B or C.
* Asynchronously to the Mutator, the GC thread can consolidate and merge maps C 
  and B into a merged map M.  (M gets then union of C's and B's keys minus B's 
keys whose value was TOMBSTONE, then all data at the addresses get relocated 
and the addresses updated.)
* Once the GC has completed its consolidation, it can inform the Mutator of M, 
  which can then atomically do:  C=M, B=A, A=(empty) (these should be seen as 
cheap pointer-like assignments, not recursive actions).
* This cycle repeats over time, such that the A map migrates to B, which will 
  eventually get merged by the GC with C, allowing the Mutator to never have to 
stop for GC consolidations.

#### Q: "Won't the deallocation of the old C and B maps cause work for the Mutator?"
A: No, we can deallocate C and B en-masse based on how allocations will only 
have increasing offsets and how we calculate our used-data-range of the 
ring-buffer.  The used-data-range of the ring-buffer goes from `LOW = 
min(lowest_offset(A), lowest_offset(B), lowest_offset(C))` to `HIGH = cursor` 
(the ring-buffer's next-allocation cursor).  As each C map was formerly a B 
map, and each B map was formerly an A map, note that the following is true for a 
given set of maps: `lowest_offset(C) < lowest_offset(B) < lowest_offset(A)`.  
Also, as the maps get reassigned by Mutator upon notifcation of GC 
consolidation, note that the following will be true: `lowest_offset(old_C) < 
lowest_offset(M) AND lowest_offset(old_B) < lowest_offset(M)`.  This is because 
M must necessarily have been constructed *after* C and B, so it will have a 
higher `lowest_offset` than both of them.  As such, upon the reassignment of 
the maps and recalculation of `LOW`, the old_C and old_B maps will exist below 
`LOW` (`LOW` having been essentially recalculated as `LOW = 
min(lowest_offset(M), lowest_offset(old_A), lowest_offset(new_A))`.  This means 
old_C and old_B now exist outside the used data range (LOW..HIGH) of the 
ring-buffer, which means those locations are considered deallocated and free 
for reuse with no further action.

#### Q: "Will the Mutator and GC need to contend through atomic operations on the ring-buffer's cursor?"
A: No, Mutator can initiate the GC collection by preallocating a region in the 
ring-buffer which is `size(C) + size(B)` and then passing that region to GC to 
be filled in with M.  Note that this doesn't affect M's relative placement to C 
and B:  M will still exist after C and B.

#### Q: "What happens when the offset reaches the end of the ring-buffer and as such gets modulo'd down to a lower value?  Then the above comparisons won't work, right?"
A: We keep the offsets in a pre-modulo'd state (actually, "pre-masked" as we 
use power-of-2 ring-buffer sizes), modulo'ing into the ring-buffer's memory 
only as necessary.  This allows the above comparisons to stay valid.

#### Q: "OK, but what happens when your offset's datatype overflows and wraps around, then the comparisons must fail, right?"
A: The above comparison operator is a simplification.  In actuality, we'll be 
using Serial Number Arithmetic (see below) which will prevent problems with 
wrap-around comparisons.

#### Q: "You've been shifting between addresses and offsets in your descriptions, could you clarify?"
A: Yes, sorry, I am trying to ease concept introduction.  An allocations within 
the ring-buffer will return an offset, not an address.  However, this offset is 
efficiently resolvable into an address: `ADDRESS = (offset & 
(ringbuffer.power_of_2_size - 1))`.  The purpose of using offsets instead of 
addresses is to abstract over ring-buffer resizes (the offsets will still apply 
to the larger ring-buffer, but resolve to a new actual address).  Above, where 
we create A, B, and C maps of `handle -> address` it is actually more correct 
to say they are `handle -> offset` maps.

#### Q: "It seems you'd want to use O(1) hashtables for the map implementation.  Wouldn't there be issues with choosing the right size, or or otherwise wouldn't the C map need an occasional large pause for a resize?"
A: Yes, it is true that an O(1) hashtable has these undesirable properties.  We 
will use for the map implementation a structure called the Array-Mapped Trie by 
Phil Bagwell that can give us "effective O(1)" complexity (in reality O(log32 
n)).  It is essentially a very shallow tree with a large (32-way) branching 
factor.  It has useful properties of not needing resizes and of easily being 
made persistent.

#### Q: "I see how the 3 map implementation suffices for dereferencing of handles given allocations and deallocations, but programs will also want to *modify* structures via their handles.  Wouldn't it cause problems if the Mutator would be modifying structures while the GC is observing them for its consolidation?"
A: Yes, this is another several points I glossed over.  The runtime will 
provide persistent data types such as an O(log n) RB-tree, an O(log32 n) 
hashmap, etc.  A mutation to a persistent data type instance is done via 
path-copying and produces a new instance version as represented by an offset to 
the root of the newly written path (with nodes along this path pointing to 
subsections of the older instances for data-sharing).  Because of this, the 
state of older instance versions will remain in place and still under GC 
observations even as new instance versions are derived from them.  (The 
alternative "fat-pointer" method of constructing persistent data structures 
would require undesirable mutations and so is avoided.)  So modifications to 
the persistent data types will be fine.
It is anticipated that the 3-map mechanism will only be used for containing 
entries to tuples which will be used to compose fields of primitive data types 
and persistent data types.  These tuples can be seen as analogous to structs in 
the C language.  One of these primitive data types would be "handle" which can 
be dereferenced through the 3-map set.  Read-only dereferences of handles would 
work fine.  Dereferences for mutation would be more involved: To modify a field 
of the tuple at handle `h`, it would be required to first copy the tuple 
forward with the modified field to new offset `o` and then place a new entry `h 
-> o` into map C.  Having a maximum arity for tuples (say of 256) would bound 
the work of this copy forward.

#### Q: "That sounds like a lot of tuple copying!"
A: Yeah, actually we won't actually copy the tuple for each field modification, 
we will use something I've termed the `cutoff_offset` to minimize the need for 
copying.  The `cutoff_offset` is an offset within the ring-buffer which 
represents the following property:  offsets lower than the `cutoff_offset` must 
remain immutable, offsets higher than the `cutoff_offset` can be mutated in 
place.  The `cutoff_offset` is maintained by the Mutator as needed, so as to 
lock down previous writes.  

A simple example of a use case for `cutoff_offset` could be the following: data 
at offsets less than `cutoff_offset` have been exposed to the GC thread via a 
collection request, data at offsets greater than the `cutoff_offset` are 
exclusive to the Mutator, and when the Mutator transmits to the GC thread it 
updates `cutoff_offset` to point to the ring-buffer's cursor (exposing all 
previously-written data and forcing tuples to be copied-forward for updates).

Once an area of data has been locked down via the `cutoff_offset` it can no 
longer be modified, though the data will eventually be deallocated once 
observers of it can no longer exist.  As a tuple is copied forward its new copy 
must necessarily exist at an offset greater than the `cutoff_offset`.  The 
tuple's offset being greater than the `cutoff_offset` implies that there can be 
no other observers of this tuple and so it is safe to modify it in place with 
no further need to copy forward nor add/update an entry in map C.  So 
copy-forwards of tuples are only needed and performed for tuples whose offset 
is less than `cutoff_offset`.  If the Mutator does something to increase the 
`cutoff_offset` such that it now encompasses the tuple as "locked down" then 
the target tuple must be copied forward beyond the `cutoff_offset` before its 
copy can then be modified.

#### Q: "You mentioned resizing of the ring-buffer... Wouldn't this cause pauses?"
A: Yes, CB eliminates pauses due to GC consolidation/defragmentation but it 
does not eliminate pauses due to enlarging the amount of working memory.  These 
enlargements are performed by resizing the ring-buffer larger and will need to 
cause a pause.  It is expected that an appropriate ring-buffer size can be 
reliably estimated/overestimated to be used for the initial size at VM startup
Remember that with power-of-2 ring-sizing we are talking about an estimate of 
the order of magnitude!  Also, a ring-buffer resize pause is only as long as it 
takes for several mmap()s and a memcpy() of the smaller ring-buffer's contents 
into the larger one, so it should be relatively quick.

#### Q: "Wouldn't you have to check ring-buffer wrap-around of every read and write?"
A: Not with a "magic ring buffer"!  Please see a description below.  Basically, 
there will be a guarantee that reads/writes from/to the ring-buffer below the 
loop size will be contiguously visible in virtual memory due to the loop 
remapping the first N pages of the ring-buffer.  So long as our data structures 
are composed of substructures which remain smaller than this guarantee, we will 
never have to check for wrap-around (i.e. discontiguous reads/writes).  This 
does, however, mean, that thinks like arrays and hashmaps MUST be either 
unusually short or composed of shallow tree persistent data structures (which 
seem at best to achieve O(log32 N) complexity).

#### Q: "What is the complexity of this 3-map mechanism?"
A: The complexity of the 3 map lookup is the same as the complexity of the 
chosen map implementation itself because there is a constant factor of 3 
layers.  So, in our case, O(log32 n).  This doesn't mean that such constant
factors won't become limiting factors, unfortunately, and this is part of the 
experiment.


## Illustration of Mechanism

The algorithm requires 3 key-value maps: A, B, C.  In the initial state, these 
each begin empty:

```
    C            B            A
   ---          ---          ---
   (empty)      (empty)      (empty)
```

New allocations of class instances are assigned in A.  Here the program has 
allocated three new instances:

```
    C            B            A
   ---          ---          ---
   (empty)      (empty)      0 -> 0x10
                             1 -> 0x20
                             2 -> 0x30
```

Deallocations of instances are not removed from A, instead a tombstone value 
indicating non-presence is stored.  Here the program has deallocated instance 
1:

```
    C            B            A
   ---          ---          ---
   (empty)      (empty)      0 -> 0x10
                             1 -> TOMBSTONE
                             2 -> 0x30
```

At this point, the main program thread (a.k.a. the Mutator) intitates a garbage 
collection (e.g.  due to a timer, or a number of function calls, or some other
reason).  This will allow the separate garbage collector thread to begin the 
work of compacting the full set of items still in existence in B and C.

Message from Mutator To GC:  `{ do_collection, B, C }`

The main program thread (a.k.a. the Mutator) continues to run and modify A:

```
    C            B            A
   ---          ---          ---
   (empty)      (empty)      0 -> 0x10
                             1 -> TOMBSTONE
                             2 -> 0x30
                             3 -> TOMBSTONE
                             4 -> 0x50
```

The Garbage Collector thread replies to the Mutator thread with its resultant 
consolidation.  This is represented by a new map called newB which contains the 
union of all entries in B or C, minus any keys which had a TOMBSTONE in B.  (B 
acts as an overlay of C).  Also, the address value for each remaining key has 
has been rewritten to point to a new, relocated address.

```
    newB
   ------
   (empty)
```

Message from GC to Mutator:  `{ collection_done, newB }`

At this point, the Mutator incorporates the GC-consolidated memory into its 
worldview.  These assignments happen atomically as far as main program 
execution is concerned:

```
A remains the same
B = newB
C = (empty)

    C            B                  A
   ---          ---                ---
   (empty)      0 -> 0x10          (whatever it already contained)
                1 -> TOMBSTONE
                2 -> 0x30
                3 -> TOMBSTONE
                4 -> 0x50
```

Let's do a few more allocations:

```
    C            B                  A
   ---          ---                ---
   (empty)      0 -> 0x10          5 -> 0x60
                1 -> TOMBSTONE     6 -> 0x70
                2 -> 0x30
                3 -> TOMBSTONE
                4 -> 0x50
```
                
Now another merge...

```
    newB
   ------
   0 -> 0x80
   2 -> 0x90
   4 -> 0xA0
```

... while A continues to be modified...

```
    C            B                  A
   ---          ---                ---
   (empty)      0 -> 0x10          2 -> TOMBSTONE
                1 -> TOMBSTONE     6 -> 0x70
                2 -> 0x30          7 -> 0xB0
                3 -> TOMBSTONE
                4 -> 0x50
```
                
... another map reassignment...

```
    C            B                  A
   ---          ---                ---
   0 -> 0x80    2 -> TOMBSTONE    (empty)
   2 -> 0x90    6 -> 0x70
   4 -> 0xA0    7 -> 0xB0
```

... another merge...

```
    M
   ---
   0 -> 0xC0
   4 -> 0xD0
   6 -> 0xE0
   7 -> 0xF0
```

... while A continues to be modified...

```
    C            B                  A
   ---          ---                ---
   0 -> 0x80    2 -> TOMBSTONE     8 -> 0x100
   2 -> 0x90    6 -> 0x70          9 -> 0x110
   4 -> 0xA0    7 -> 0xB0
```

... and finally, another map reassignment.

```
    C            B                  A
   ---          ---                ---
   0 -> 0xC0    8 -> 0x100         (empty)
   4 -> 0xD0    9 -> 0x110
   6 -> 0xE0
   7 -> 0xF0
```

Note here several things:
1) TOMBSTONEs get removed from C and B as they get merged to newB, as newB will no 
longer need to overlay any other map with its deletions once newB is installed as 
the new B.
2) The A map proceeded with modifications as newB was being built.
3) During the first collection iteration, the instance 2 has been deleted in 
the A map, even as newB is also concerned with instance 2 for the collection.  The 
overlay semantics guarantees that this will not be a problem.
4) During the first allocation The A map allocations continued at 0xB0, while newB 
used 0x80, 0x90, and 0xA0.  This is because the initiation of the GC was able 
to allocate an appropriately sized region for M to allocate within and bump the 
cursor ahead by this amount.  The M map allocates from within this region 
arranged for it, and the A map allocates beyond it.
5) Omitted in this illustration:  the map implementation will have its own 
internal nodes, so addresses would not appear so regularly-spaced; TOMBSTONEs 
take up space as they require aforementioned internal nodes; the sizing of newB 
may be a maximal size and not as accurate as portrayed here; allocations within 
newB's region would be most efficient when moving the cursor in reverse, as this 
would allow maximizing newB's lowest_bound (further packing the ring-buffer's 
still-in-use data towards upper offsets and thereby maximizing the available 
data range).


## Dereferencing Pseudocode

```
void* offset_to_address(offset)
{
    return ring.baseptr + (offset & (ring.power_of_2_size - 1));
}

offset dereference_handle_for_read(handle)
{
    if (A[handle] exists) {
        if (A[handle] == TOMBSTONE)
            return NULLOFFSET;

        return A[handle];
    }

    if (B[handle] exists) {
        if (B[handle] == TOMBSTONE)
            return NULLOFFSET;

        return B[handle];
    }

    if (C[handle] exists) {
        /* Cannot be TOMBSTONE, as these will be cleared by consolidation */
        return C[handle];
    }

    return NULLOFFSET;
}

offset dereference_handle_for_write(handle)
{
    if (A[handle] exists) {
        if (A[handle] == TOMBSTONE)
            return NULLOFFSET;

        return A[handle];
    }

    if (B[handle] exists) {
        if (B[handle] == TOMBSTONE)
            return NULLOFFSET;

        new_offset = copy_instance_at(B[handle]);
        A[handle] = new_offset;
        return new_offset;
    }

    if (C[handle] exists) {
        /* Cannot be TOMBSTONE, as these will be cleared by consolidation */
        new_offset = copy_instance_at(C[handle]);
        A[handle] = new_offset;
        return new_offset;
    }

    return NULLOFFSET;
}
```

## Concepts / Glossary

### Dynamic Memory Allocation
Programs need to use memory to perform their duties.  Apportioning memory for 
use by a program is called memory allocation.  Some needed allocations can be 
known in advance at time of programming, but for many (and perhaps most) 
programs the needed allocations can only be determined during runtime.  The 
former set is considered "static memory allocation" and the latter set is 
called "dynamic memory allocation".

In unmanaged and non-garbage-collected languages like C and C++, any dynamic 
allocation of memory is the programmer's responsibility to free.  e.g.  in C:
```
   void *p = malloc(5);  //Allocate 5 bytes, returning a pointer to them.
   //...some work...
   free(p);   //Deallocate the allocated bytes.
```
Note that both `malloc()` and `free()` may perform some not-insubstantial 
amount of work during their invocations.  There are many opportunities for 
programmer error in the pairing of allocations and deallcations, especially as 
these two halves become further apart in code and/or time.

Benefit of Dynamic Memory Allocation:
* Dynamic Memory Allocation is the only solution for problems whose allocations 
  can only be known at runtime.

Drawback of Dynamic Memory Allocation:
* Can add a significant fast-path cost for both the `malloc()` and `free()` 
  routines.
* Can cause memory fragmentation. This is where available memory exceeds a 
  requested allocation size, but this available memory is not contiguous so 
cannot be used to fulfill the allocation request.


### Garbage Collection
Garbage-collection is a technology which allows automatic reclamation of 
dynamically allocated memory which has become no longer in use.  In 
garbage-collected languages, the programmer still specifies dynamic allocations 
(via something analoguous to `malloc()`, though a given language's syntax may 
not make such allocations obvious), but the programmer does not specify an 
equivalent of `free()`.  Instead, the language runtime performs 
garbage-collection of dynamically allocated memory which can be detected to be 
no-longer used.  This detection is often done via a reachability analysis of 
the memory structures of the program.  As in, there is a section of code of the 
language runtime called "the garbage collector" which will start from a root 
set of "definitely reachable" structures (e.g. static variables, and all of the 
stack-allocated references of all threads' stack frames) and then recursively 
introspect structures and chase pointers until it has fully exhausted the set 
of all reachable memory which it then considers "still-in-use".  At this point, 
the garbage collector can consider all unreachable memory addresses as now 
available for re-use by future allocations.  To reduce fragmentation of 
available memory, there will often also be a "compaction" of the "still-in-use" 
memory.  This will consolidate the still-in-use structures into a more 
condensed arrangement with the corresponding intent that the free areas become 
contiguous and can therefore be used to fulfill larger future allocations than 
if they had remained small and discontiguous.

Benefits of Garbage Collection:
* Reduces concerns needing to be managed by the programmer, leading to faster 
  coding cycles.
* Reduces chances of error by programmer (i.e. lack of `free()` causing memory 
  leak, double-`free()` causing memory corruption).
* The `free()` can happen asynchronously to the main body of code, potentially 
  taking its work off of the fast path and reducing latency
* Memory consolidating garbage collectors allow for lower fragmentation of 
  available process memory, increasing efficiency (smaller memory footprint in 
terms of VM pages, fewer trips to OS to ask for more memory).

Drawbacks of Garbage Collection:
* The work of collecting garbage historically must cause pauses in the work of 
  the application.  These pauses can be minimized (as with advanced techniques 
such as Erlang's runtime, which can use ultra-small GC cycles limited to the 
memory of a single green-thread) but they still exist and cause latency jitter 
in the execution of applications.

References:
_The Garbage Collection Handbook: The Art of Automatic Memory Management_ by 
Richard Jones, Antony Hosking, Eliot Moss


### Arena-Based Allocation
(a.k.a. "Bump-pointer Allocation", "Sequential Allocation", "Region-Based 
Allocation")

As mentioned above, both `malloc()` and `free()` routines have a potentially 
significant cost.  This is especially true if they are done multiple times or 
if they are done on the fast-path (a latency-sensitive portion of an 
application's code).  Arena-based allocation is an alternative approach for 
dynamic memory allocation which is sometimes used.

```
//Allocate a 10MB arena.
char *arena = malloc(10 * 1024 * 1024);
char *arena_cursor = arena;

// Later... Perform cheap dynamic allocation(s).
item *item = arena_cursor;
arena_cursor += sizeof(item);
other_item *other_item = arena_cursor;
arena_cursor += sizeof(other_item);

//Possibly reset the arena to reuse its space.
arena_cursor = arena;

//Later... Deallocate the entire arena and any contained items at once.
free(arena);
```

Advantages of Arena-Based Allocation:
* Aside from the initial `malloc()`, the cost of any dynamic allocation has 
  been reduced from the cost of a `malloc()` call to the cost of adding a value 
to a pointer.  The cost of adding a value to a pointer is so cheap as to be 
effectively free.
* Multiple dynamic allocations have paid only a single cost of `malloc()`.
* Multiple dynamic deallocations have paid only a single cost of `free()`.
* Arena-allocated items do not need to be of equal size.

Disadvantages of Arena-Based Allocation:
* Must consider appropriate initial size of arena.
* Must consider dynamic allocations exhausting the size of the arena.
* Individual deallocations of items is unsupported.  (It can be supported but 
  complexifies and slows allocations, and would generally only be appropriate 
for arenas which allocate items of a single size.)

Reference: [Wikipedia: Region-Based Memory Management] (https://en.wikipedia.org/wiki/Region-based_memory_management)


### Ring Buffers
A ring buffer is a data structure implemented as a contiguous array of 
elements.  The elements of this array are accessed using cursors that will be 
mapped to the array index range `[0, count)` via modulo arithmetic.  This 
allows the cursors to perpetually advance while being used to refer to array 
elements within the valid range.  If the underlying array has a number
of elements which is a power of two, instead of the (relatively expensive) 
modulo operation a (relatively cheap) bitmask AND operation may be used as it 
will be numerically equivalent.

Ring buffers are often used with two cursors as a FIFO channel between a 
producer and a consumer.  The producer is responsible for one cursor's 
advancement, and the consumer for the other cursor's advancement.  The 
relationship `consumer_cursor <= producer_cursor` is maintained, and the range 
`[consumer_cursor, producer_cursor)` will contain produced-but-not-yet-consumed 
data.

Reference: [Wikipedia: Circular Buffer](https://en.wikipedia.org/wiki/Circular_buffer)


### Magic Ringbuffers
(a.k.a. Virtual Ring Buffers)

Ring buffers may be used to contain slotted elements of data (e.g.  slots of 
pointers), or could alternatively be used as a character array stream.  For the 
character array stream case, writing a logically contiguous region of memory 
into the ring buffer may necessitate a two part write if the placement of
to-be-written-region were to surpass the last index within the ringbuffer and 
as such need to wrap-around to earlier indices.  In addition to the two part 
write, bounds checking operations need to take place during every read and 
write, to ensure that this "wrapping around" will take place when appropriate. 

Philip Howard seems to be (according to my Google searches) the inventor of the 
following optimization:  Memory management APIs (e.g.  `mmap()` on Linux), can 
be used to cause the same physical page of memory (i.e. hardware memory pages) 
to be made available through multiple virtual memory addresses (i.e. the 
addresses which are used within a C program's memory address space).  As such, 
the entire ringbuffer pages could be `mmap()`'d again immediately subsequent to 
their original location.  E.g.:

```
Simple Ring Buffer
    [ A | B | C | D ]

Magic Ring Buffer
    [ A | B | C | D | A' | B' | C' | D' ]

                    \-------remap-------/

The A'...D' pages are the exact same physical pages as A...D, just available 
also under other virtual addresses.  A write in B' will show up in B and vice 
versa.

```

With this layout, no two-part writes nor bounds checking need to take place 
during reads or writes.  Any write or read less than the length of the 
ring-buffer must begin within A...D (due to the ring buffer's modulo operation) 
and end within A'...D'.  Due to the second mapping of these pages, the 
wraparound reads/writes will be automatic and no bounds checks will be 
necessary.

Reference: [Virtual Ring Buffers by Philip Howard] (http://vrb.sourceforge.net/)


### Serial Number Arithmetic
It is often useful to compare two sequence numbers to determine which is 
greater than another.  In infinite or very large sequences with sequence 
numbers represented by values of finite size (e.g. hardware 32-bit integer 
registers), eventually deriving the next sequence number will "wrap-around" 
such that direct comparison between sequence numbers is inadequate to represent 
their ordering.

The solution to this problem is called "Serial Number Arithmetic".  Basically, 
value1 is greater than value2 if value1 exists in the half of the total range 
of values (i.e. 2^31) subsequent to value2, even if this half-the-total-range 
wraps around to lower values.  Likewise, but in the lower-half, for value1 
being less-than value2.  This forms somewhat of a finite "sliding window" for 
working with sequences larger than that window.

Reference: [Serial Number Arithmetic] (https://en.wikipedia.org/wiki/Serial_number_arithmetic)
Reference: [RFC 1982] (https://tools.ietf.org/html/rfc1982)


### Persistent Data Structures
TODO

### Persistent Red-black Tree
TODO

### Persistent HAMT
TODO

### The Cutoff Offset
TODO

### Green-Threads
TODO

### Alternative to Address-based Pointers
Conceptually, a pointer in C is a unique handle through which you can access 
your data via "dereferencing" it.  Mostly, C language implementations leverage 
the fact that your data will exist somewhere in memory and that the address of 
your data's location will already be a suitably unique value, so the address is 
what gets used as a handle.  Hardware instructions will ultimately require an 
address, so using the address as a handle dovetails with hardware details and 
allows avoidance of a separate lookup of `handle -> address`.

However, using addresses is not required for pointers.  Instead, an 
implementation could choose to use some other form of identifier (say sequence 
numbers), so long as dereferencing actions then included such a `handle -> 
address` lookup.

Reference: [What exactly is a C pointer if not a memory address?]( http://stackoverflow.com/questions/15151377/what-exactly-is-a-c-pointer-if-not-a-memory-address/15151542#15151542 )

### cb_offset_t
TODO

### The Structmap
The structmap is an implementation of the "Alternative to Address-based 
Pointers", as described above.  Instead of "pointers represented by addresses" 
there will be "references represented by integer struct_ids". The idea is that
dynamic allocations will be assigned unique sequential integer identifiers 
derived from a monotonically increasing `next_struct_id`.  These allocations 
will also place a new entry in the structmap, which will be used for later 
`struct_id -> cb_offset_t` "dereferences".

## Roadmap
TODO


vim: fo=cqtaw2l
