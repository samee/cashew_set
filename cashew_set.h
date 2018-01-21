/* 19 Jan, 2018: Check how this all compares with Boost fast pool allocator.

   25 Dec, 2017: Right now, we need --std=c++1z for this to compile. Clang++
   (v4.0.1) fails without it because of constexpr, while g++ (v7.2.0) seems to
   succeed succeeds but silently ignores dynamic memory allocation alignment
   requirements. Actually, g++ just corrupts everything because non-trivial
   destructors don't work well with over-aligned types.
   We do have a runtime check for that.


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
   general, a node with node.count elements have node.elt_count+1 children,
   unless node.children==nullptr. In general, nodes can be in one of three
   states:
   
     * Empty node: node.count == 0 && node.children == nullptr
     * Leaf node: 0 <= node.elt_count <= elt_count_max &&
                  node.children == nullptr
     * Non-leaf node: 0 <= node.elt_count <= elt_count_max &&
                      node.children != nullptr

   17th Dec. 2017: Hmm, I just allowed discontiguous data population in trees,
   where it is possible to have a chain of nodes with no elements. Inserted
   nodes get added to the leaf at the bottom of that chain. I don't feel too
   good about this, but the logic is too uniform to ignore. I might regret
   this.

   The fact that we even have nodes with no elements is a consequence of always
   splitting nodes by the value being inserted, and not the median value.
   Inserted values can be smaller or larger than all other values in the node.

   TODO obsolete doc, need to update. 15th Dec. 2017.
   We never keep internal nodes with zero elements. The node.children pointer
   always points to an array of SetInt32Node[setInt32ChlidrenPerNode]; Depending
   on node.count, the last few elements of this array may be unused: we always
   keep them zero-initialized anyway.

   Elements in a single node are not sorted. Instead, when searching for a key
   x, we determine which child to proceed to by counting the number of index i
   that satisfies elt[i] < x. This is a set, not a multiset, so if x is already
   found in a node, we do not have to proceed any farther.

   This, however, means that we have to be careful when a child node gets split.
   The children have to be shifted to make room in the child array.


   Things left to consider
   -----------------------

   We want a forest: consider shared_ptr or other mechanisms. Code may have to
   be more generic than what we thought. Right now, we are using unique_ptr.

   Maps will need a trailing array of values, but that needs flexibility in the
   children layout described. We may need something special for root node, to
   preserve index locality.
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
//#include <functional>  // Creates a problem for clang++ v4.0.1
#include <limits>
#include <memory>

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
struct alignas(Traits::cache_line_nbytes) CashewSetNode {
  using key_type = typename Traits::key_type;
  using elt_count_type = typename Traits::elt_count_type;
  static constexpr elt_count_type elt_count_max = Traits::elt_count_max;
  std::unique_ptr<CashewSetNode[]> children;
  elt_count_type elt_count;
  key_type elts[elt_count_max];

  CashewSetNode() : children(nullptr), elt_count(0) {
    static_assert(sizeof(CashewSetNode) == Traits::cache_line_nbytes,
        "Tree nodes do not match cache size");
  }
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
    root.children.reset();
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
    std::unique_ptr<node_type[]> family0, family1;
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
  static std::unique_ptr<node_type[]> make_family() {
    auto rv = std::make_unique<node_type[]>(node_type::elt_count_max+1);
    if ((ptrdiff_t(rv.get()) & (Traits::cache_line_nbytes-1)) != 0)
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
  return node.children==nullptr?0:countRecursive(node.children[lessCount],key);
}

// Return value indicates if key was just inserted, or it had already existed.
// Note to future me: tryInsert should return nullptr parts if root.children
// starts out as nullptr.
template <class Elt, class Less, class Eq, class Traits>
bool cashew_set<Elt,Less,Eq,Traits>::insert(key_type key) {
  auto result=tryInsert(root,1,key);  // 1 == depth of root node.
  if(result.status != InsStatus::familySplit)
    return result.status != InsStatus::duplicateFound;

  // People, we have bad news. tryInsert() has split our family.
  // Step 1) Fix pointers.
  root.children = make_family();
  root.children[0].children=std::move(result.family0);
  root.children[1].children=std::move(result.family1);

  // Step 2) Split up elts.
  root.children[0].elt_count=splitArray(root.elts,root.elt_count,
      root.children[0].elts,root.children[1].elts,key,less);
  root.children[1].elt_count=root.elt_count-root.children[0].elt_count;

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
  if(nodeDepth==treeDepth && node.children!=nullptr)
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
//     node.children == nullptr
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
    if(node.children==nullptr) node.children = make_family();

    auto result = tryInsert(node.children[lessCount],nodeDepth+1,key);
    if(result.status!=InsStatus::familySplit) return result;

    // O(n) insert of result.family into node.children,
    // at position lessCount+1.
    const elt_count_type child_count = node.elt_count+1;
    shiftArray(node.children.get()+lessCount+1,child_count-lessCount-1);
    node_type &lt_node = node.children[lessCount];
    node_type &gt_node = node.children[lessCount+1];
    lt_node.children = std::move(result.family0);
    gt_node.children = std::move(result.family1);

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
//     node.children == nullptr
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
  if(node.children==nullptr)
    throw cashew_set_bug("Full leaf node should only appear at leaf level");

  auto result = tryInsert(node.children[lessCount],nodeDepth+1,key);
  if(result.status!=InsStatus::familySplit) return result;

  const elt_count_type child_count = node.elt_count+1;
  auto nibling = make_family();

  // Let our larger children be adopted by the new sibling family.
  move_n(node.children.get()+lessCount+1, child_count-lessCount-1,
         nibling.get()+1);
  node_type &lt_node=node.children[lessCount];
  node_type &gt_node=nibling[0];
  lt_node.children=std::move(result.family0);
  gt_node.children=std::move(result.family1);

  // Distribute node.elts
  gt_node.elt_count =
    lt_node.elt_count-splitArray(lt_node.elts,lt_node.elt_count,
        lt_node.elts,gt_node.elts,key,less);
  lt_node.elt_count-=gt_node.elt_count;
  return {std::move(node.children),std::move(nibling),InsStatus::familySplit};
}

}  // namespace cashew
