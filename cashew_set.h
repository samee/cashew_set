/* 21 Jan, 2018: We can now compile this with --std=c++11, thanks to
   aligned_unique_ptr. This works both with g++ and clang++.

   Intro
   -----

   Implements a balanced tree set structure, while trying to be cache friendly.
   It assumes 64-byte cache lines. Right now, that seems to be the going
   size: in Intel, AMD, and even ARM processors, through all levels of their
   cache hierarchies. IBM Power processors are a notable exception to this rule,
   which has 128-byte cache lines. Thus it's only useful for small data types.
 

   Data layout
   -----------

   The tree structure is a B-tree.

   Each node is exactly 64-bytes long, and we try to fit as much as possible in
   it. The layout accommodates both 32-bit and 64-bit pointers. For example, if
   we are storing int32_t, each node has 14 or 13 elements. See
   CashewSetTraits::elt_count_max below.

   Only a single pointer is stored to make room for more data elements. In
   general, a node with node.elt_count elements have node.elt_count+1 children,
   unless node.family ==nullptr. In general, nodes can be in one of three
   states:
   
     * Empty node: node.count == 0 && node.family == nullptr
     * Leaf node: 0 <= node.elt_count <= elt_count_max &&
                  node.family == nullptr
     * Non-leaf node: 0 <= node.elt_count <= elt_count_max &&
                      node.family != nullptr

   17th Dec. 2017: Hmm, I just allowed discontiguous data population in trees,
     where it is possible to have a chain of nodes with no elements. Inserted
     nodes get added to the leaf at the bottom of that chain. I don't feel too
     good about this, but the logic is too uniform to ignore. I might regret
     this.

     The fact that we even have nodes with no elements is a consequence of
     always splitting nodes by the value being inserted, and not the median
     value.  Inserted values can be smaller or larger than all other values in
     the node.

   The node.family pointer always points to an array of
   node_type[elt_count_max+1]; Depending on node.elt_count, the last few
   elements of this array may be unused: we always keep them zero-initialized
   anyway.

   Elements in a single node are not sorted, linear search seems good enough.
   However, if we don't find an element at a node, we still need to figure out
   which child to proceed to. We determine this by computing the position it
   would have taken, had the elements in the node been sorted. So when looking
   for an element x, we proceed to family.child[c], where c is the the number of
   index i that satisfies elts[i] < x. This is a set, not a multiset, so if x is
   already found in a node, we do not have to proceed any farther.

   This, however, means that we have to be careful when a child node gets split.
   The children have to be shifted to make room in the child array.


   Things left to consider
   -----------------------

   We might want a forest: consider shared_ptr or other mechanisms. Code may
   have to be more generic than what we thought. Right now, we are using
   unique_ptr.

   Maps will need a trailing array of values, but that needs flexibility in the
   children layout described. We may need something special for root node, to
   preserve index locality.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>

#include "aligned_unique.h"

namespace cashew {

static_assert(sizeof(void*)==4 || sizeof(void*)==8,
    "CashewSet currently only supports 32-bit or 64-bit pointers");

template <class Elt>
struct CashewSetTraits {
  // Hardcoded things.
  using key_type=Elt;
  using elt_count_type=int8_t;
  static constexpr int cache_line_nbytes = 64;

  // Computed things.
 private:
  static constexpr size_t elt_count_max_size_t = 
    (cache_line_nbytes-sizeof(void*)-sizeof(elt_count_type)) / sizeof(Elt);
 public:
  static_assert(std::numeric_limits<elt_count_type>::max() >=
                elt_count_max_size_t+1,
                "elt_count_type is too short");
  static constexpr elt_count_type elt_count_max =
    elt_count_type(elt_count_max_size_t);
  static constexpr elt_count_type children_per_node = elt_count_max+1;
};

template <class Elt, class Traits>
struct CashewSetNode {
  using key_type = typename Traits::key_type;
  using elt_count_type = typename Traits::elt_count_type;
  static constexpr elt_count_type elt_count_max = Traits::elt_count_max;

  // We don't directly use aligned_unique_ptr<T[]>, and instead wrap T[] in
  //   a struct. This is because (a) {aligned_,}unique_ptr<T[n]> is not defined
  //   for some unknown reason, and (b) T[] specializations use a bit more
  //   memory to track array length, so they can call destructors properly.
  struct family_type;
  using family_pointer_type = aligned_unique_ptr<family_type>;
  family_pointer_type family;
  elt_count_type elt_count;
  key_type elts[elt_count_max];

  CashewSetNode() : family(nullptr), elt_count(0) {
    static_assert(sizeof(CashewSetNode) == Traits::cache_line_nbytes,
        "Tree nodes do not match cache size");
  }
};

template <class Elt, class Traits>
struct CashewSetNode<Elt, Traits>::family_type {
  CashewSetNode child[elt_count_max+1];
};

struct cashew_set_bug : std::logic_error {
  explicit cashew_set_bug(const char* what) : std::logic_error(what) {}
};

// Comparisons are assumed cheap. The same two elements may be compared
// repeatedly to each other.
template <class Elt, class Less = std::less<Elt>,
          class Eq = std::equal_to<Elt>,
          class Traits = CashewSetTraits<Elt>>
class cashew_set {
 public:
  using key_type = typename Traits::key_type;
  using value_type = typename Traits::key_type;
  using size_type = size_t;
  bool insert(key_type key);
  void clear() noexcept {
    root.elt_count=0;
    root.family.reset();
    treeDepth = 1;
    treeEltCount = 0;
  }
  size_type count(key_type key) const { return countRecursive(root,key); }
  size_type size() const noexcept { return treeEltCount; }
  bool empty() const noexcept { return treeEltCount==0; }
 private:
  using depth_type = int8_t;  // One byte is *plenty*.
  using elt_count_type = typename Traits::elt_count_type;
  using node_type = CashewSetNode<Elt,Traits>;
  using family_type = typename CashewSetNode<Elt,Traits>::family_type;
  node_type root;
  Less less;
  Eq eq;
  depth_type treeDepth = 1;      // We start counting at root depth == 1.
  size_type treeEltCount = 0;

  void checkBugs(const node_type& node, depth_type nodeDepth) const;
  int countRecursive(const node_type& node, key_type key) const;

  // Insert method helpers.
  enum class InsStatus {done, duplicateFound, familySplit};
  struct TryInsertResult {
    // Note to future self: I could have saved a few cycles in
    // insertSpacious if we were to return only one of these,
    // since family0 doesn't really change.
    aligned_unique_ptr<family_type> family0, family1;
    InsStatus status;
  };
  TryInsertResult insertSpacious(
      node_type& node,depth_type nodeDepth,key_type key,
      elt_count_type lessCount);
  TryInsertResult insertFull(
      node_type& node,depth_type nodeDepth,key_type key,
      elt_count_type lessCount);
  TryInsertResult tryInsert(
      node_type& node,depth_type nodeDepth,
      key_type key);
  static aligned_unique_ptr<family_type> make_family() {
    auto rv = make_aligned_unique<family_type, Traits::cache_line_nbytes>();
    if ((ptrdiff_t(rv->child) & (Traits::cache_line_nbytes-1)) != 0)
      // This should be a warning, not an error. But right now,
      // this indicates a GCC problem that causes memory corrption.
      throw cashew_set_bug("new[] produced unaligned tree nodes. "
          "This may have to be recompiled with --std=c++1z.");
    return std::move(rv);
  }
};

// Splits up src into two arrays, dest_lt and dest_ge, depending on whether the
// element is less than or greater or equal to pivot.
// It is okay if src==dest_lt or src==dest_eq, but assumes dest_lt and dest_ge
// don't overlap. Also assumes no partial overlap: either full alias, or no
// overlap with src. Assumes both dest_lt and dest_ge has enough space.
template<class X, class Len, class Less>
Len splitArray(const X src[],Len len,X dest_lt[],
               X dest_ge[],X pivot,Less less) {
  Len i, ltCount=0;
  for(i=0;i<len;++i)
    if(less(src[i],pivot)) dest_lt[ltCount++]=src[i];
    else dest_ge[i-ltCount]=src[i];
  return ltCount;
}

// Returns 0 or 1.
template <class Elt, class Less, class Eq, class Traits>
int cashew_set<Elt,Less,Eq,Traits>::countRecursive(
    const node_type& node, key_type key) const {
  elt_count_type lessCount = 0;
  for(elt_count_type i=0;i<node.elt_count;++i)
    if(eq(node.elts[i],key)) return 1;
    else if(less(node.elts[i],key)) lessCount++;
  return node.family==nullptr
    ?0:countRecursive(node.family->child[lessCount],key);
}

// Return value indicates if key was just inserted, or it had already existed.
// Note to future me: tryInsert should return nullptr parts if root.family
// starts out as nullptr.
template <class Elt, class Less, class Eq, class Traits>
bool cashew_set<Elt,Less,Eq,Traits>::insert(key_type key) {
  auto result=tryInsert(root,1,key);  // 1 == depth of root node.
  if(result.status != InsStatus::familySplit)
    return result.status != InsStatus::duplicateFound;

  // People, we have bad news. tryInsert() has split our family.
  // Step 1) Fix pointers.
  root.family = make_family();
  root.family->child[0].family=std::move(result.family0);
  root.family->child[1].family=std::move(result.family1);

  // Step 2) Split up elts.
  root.family->child[0].elt_count=splitArray(root.elts,root.elt_count,
      root.family->child[0].elts,root.family->child[1].elts,key,less);
  root.family->child[1].elt_count=root.elt_count
      - root.family->child[0].elt_count;

  // Step 3) Reset root. This is the only step that increments treeDepth.
  root.elts[0]=key; root.elt_count=1;
  treeDepth++;
  treeEltCount++;
  return true;
}

// Move arr[0..len-1] to arr[1..len]. Assumes arr[] can actually hold len+1
// elements.
template<class X> void shiftArray(X* arr,size_t len) {
  for(size_t i=len;i>0;--i) arr[i]=std::move(arr[i-1]);
}

template <class Elt, class Less, class Eq, class Traits>
void cashew_set<Elt,Less,Eq,Traits>::checkBugs(const node_type& node,
    depth_type nodeDepth) const {
  if(node.elt_count > node.elt_count_max)
    throw cashew_set_bug("Node is corrupted. Element count too large.");
  if(nodeDepth > treeDepth) 
    throw cashew_set_bug("Node is deeper than it's supposed to be.");
  if(nodeDepth==treeDepth && node.family!=nullptr)
    throw cashew_set_bug("It's too deep for having children");
}

// Attempts to insert key into node. There are three possible outcomes, as
// indicated by InsStatus. If a family-split happens, it is upto the caller to
// clean that up the levels of the tree at node and above.
template <class Elt, class Less, class Eq, class Traits>
auto cashew_set<Elt,Less,Eq,Traits>::tryInsert(
    node_type& node,
    depth_type nodeDepth,
    key_type key) -> TryInsertResult {

  checkBugs(node,nodeDepth);

  elt_count_type lessCount = 0;
  for(elt_count_type i=0;i<node.elt_count;++i)
    if(eq(node.elts[i],key)) return {nullptr,nullptr,InsStatus::duplicateFound};
    else if(less(node.elts[i],key)) lessCount++;

  if(node.elt_count < node.elt_count_max)
    // There is no way this node will have to split.
    return insertSpacious(node,nodeDepth,key,lessCount);
  else
    // node.elt_count == node.elt_count_max, so we may have to split.
    return insertFull(node,nodeDepth,key,lessCount);
}

// Assumes without checking:
//   node.elt_count < node.elt_count_max
//   nodeDepth <= treeDepth
//   if (nodeDepth == treeDepth) {
//     node is a leaf
//     node.family == nullptr
//     we don't propagate farther down.
//   }
//   key has no duplicate directly in node.
//   lessCount == count_if(node.elts, node.elts+node.elt_count,
//                         bind(less,_1,key))
//     which also implies 0 <= lessCount <= node.elt_count
// Inserts key in subtree under node. Never returns familySplit.
template <class Elt, class Less, class Eq, class Traits>
auto cashew_set<Elt,Less,Eq,Traits>::insertSpacious(
    node_type& node,
    depth_type nodeDepth,
    key_type key,
    elt_count_type lessCount) -> TryInsertResult {

  if(nodeDepth<treeDepth) {
    if(node.family==nullptr) node.family = make_family();

    auto result = tryInsert(node.family->child[lessCount],nodeDepth+1,key);
    if(result.status!=InsStatus::familySplit) return result;

    // O(n) insert of result.family into node.family,
    // at position lessCount+1.
    const elt_count_type child_count = node.elt_count+1;
    shiftArray(node.family->child+lessCount+1,child_count-lessCount-1);
    node_type &lt_node = node.family->child[lessCount];
    node_type &gt_node = node.family->child[lessCount+1];
    lt_node.family = std::move(result.family0);
    gt_node.family = std::move(result.family1);

    // Divy up lt_node.elts.
    gt_node.elt_count = lt_node.elt_count - splitArray(
        lt_node.elts,lt_node.elt_count,lt_node.elts,gt_node.elts,key,less);
    lt_node.elt_count-=gt_node.elt_count;
  }

  // Append key to node.elts.
  node.elts[node.elt_count++]=key;
  treeEltCount++;
  return {nullptr,nullptr,InsStatus::done};
}

// copy_n is in standard library, move_n isn't. Facepalm.
template <class InputIt,class Size,class OutputIt>
OutputIt move_n(InputIt first,Size count, OutputIt result) {
  while(count-->0) *result++ = std::move(*first++);
  return result;
}

// Assumes without checking:
//   node.elt_count == node.elt_count_max
//   nodeDepth <= treeDepth
//   if (nodeDepth == treeDepth) {
//     node is a leaf
//     node.family == nullptr
//     we don't propagate farther down.
//   }
//   key has no duplicate directly in node.
//   lessCount == count_if(node.elts, node.elts+node.elt_count,
//                         bind(less,_1,key))
//     which also implies 0 <= lessCount <= node.elt_count
// Inserts key in subtree under node. Propagates any familySplit.
template <class Elt, class Less, class Eq, class Traits>
auto cashew_set<Elt,Less,Eq,Traits>::insertFull(
    node_type& node,
    depth_type nodeDepth,
    key_type key,
    elt_count_type lessCount) -> TryInsertResult {
  if(nodeDepth==treeDepth) return {nullptr,nullptr,InsStatus::familySplit};
  if(node.family==nullptr)
    throw cashew_set_bug("Full leaf node should only appear at leaf level");

  auto result = tryInsert(node.family->child[lessCount],nodeDepth+1,key);
  if(result.status!=InsStatus::familySplit) return result;

  const elt_count_type child_count = node.elt_count+1;
  auto nibling = make_family();

  // Let our larger children be adopted by the new sibling family.
  move_n(node.family->child+lessCount+1, child_count-lessCount-1,
         nibling->child+1);
  node_type &lt_node=node.family->child[lessCount];
  node_type &gt_node=nibling->child[0];
  lt_node.family=std::move(result.family0);
  gt_node.family=std::move(result.family1);

  // Distribute node.elts
  gt_node.elt_count =
    lt_node.elt_count-splitArray(lt_node.elts,lt_node.elt_count,
        lt_node.elts,gt_node.elts,key,less);
  lt_node.elt_count-=gt_node.elt_count;
  return {std::move(node.family),std::move(nibling),InsStatus::familySplit};
}

}  // namespace cashew
