/*****************************************************************************
*
*  PROJECT:     Eir SDK
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        eirrepo/sdk/AVLTree.h
*  PURPOSE:     AVL-tree implementation (for the native heap allocator)
*
*  Find the Eir SDK at: https://osdn.net/projects/eirrepo/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _AVL_TREE_IMPLEMENTATION_
#define _AVL_TREE_IMPLEMENTATION_

#include <sdk/eirutils.h>
#include <sdk/MacroUtils.h>
#include <sdk/rwlist.hpp>
#include <assert.h>
#include <algorithm>

// AVL tree is a sorted data structure that guarantees logarithmic-in-size add and
// remove operations while keeping item order intact. We realized that we needed
// this struct because the NativeHeapAllocator is being issued with random memory
// request sizes and we ought to decide fast in which free slot allocation might be
// possible.
// Note that this is NOT a naive implementation of AVL tree.
// Also used in the Map implementation.

// Useful to fetch an actual struct if AVLNode is a struct-member.
#define AVL_GETITEM( type, nodePtr, nodeMemb )      ( (type*)( (char*)nodePtr - offsetof(type, nodeMemb) ) )

// Has to be less-than comparable, in a total order.
struct AVLNode
{
    // TODO: find a way to segregate the AVL default fields from the nodestack field so that
    //  it is easier to maintain this thing.

    // WARNING: only use this method if you know that a node (right) is part of a tree.
    // Otherwise you perform undefined behavior.
    // Calling this method does destroy the tree ownership of "right", so make sure you
    // remove the condition that casts it into a tree.
    // WARNING: make sure that you have taken over the same value to the new object from
    // the moved-from object or else the tree will be wrecked.
    inline void moveTo( AVLNode&& right ) noexcept
    {
        AVLNode *parent = right.parent;

        // Are we a nodestack member?
        if ( parent == &right )
        {
            this->parent = this;
            this->nodestack_owner = right.nodestack_owner;
            
            AVLNode *next_listnode = right.next;

            // Because we have a linked-list that loops back to the first nodestack item
            // all pointers to members are not nullptr.
            // So fix up the links.
            next_listnode->prev = this;

            this->next = next_listnode;

            AVLNode *prev_listnode = right.prev;

            prev_listnode->next = this;

            this->prev = prev_listnode;
        }
        else
        {
            if ( parent != nullptr )
            {
                // Depends on if we are left or right of the node.
                if ( parent->left == &right )
                {
                    parent->left = this;
                }
                else if ( parent->right == &right )
                {
                    parent->right = this;
                }
                else
                {
                    assert( 0 );
                }
            }

            this->parent = parent;

            // Fix up the children.
            AVLNode *left_node = right.left;

            if ( left_node != nullptr )
            {
                assert( left_node->parent == &right );
                
                left_node->parent = this;
            }

            this->left = left_node;

            AVLNode *right_node = right.right;

            if ( right_node != nullptr )
            {
                assert( right_node->parent == &right );

                right_node->parent = this;
            }

            this->right = right_node;

            // We have the same height, too.
            this->height = right.height;

            // Fix up all nodestack items, if we are having a nodestack.
            AVLNode *owned_nodestack = right.owned_nodestack;

            if ( owned_nodestack != nullptr )
            {
                AVLNode *iter = owned_nodestack;

                do
                {
                    assert( iter->nodestack_owner == &right );

                    iter->nodestack_owner = this;

                    iter = iter->next;
                }
                while ( iter != owned_nodestack );
            }

            this->owned_nodestack = owned_nodestack;
        }
    }

    // We keep the default constructor while deleting the obvious culprits for safety.
    inline AVLNode( void ) = default;
    inline AVLNode( const AVLNode& ) = delete;
    inline AVLNode( AVLNode&& ) = delete;
    inline ~AVLNode( void ) = default;

    inline AVLNode& operator = ( const AVLNode& ) = delete;
    inline AVLNode& operator = ( AVLNode&& ) = delete;

    AVLNode *parent;            // if parent points to itself, then we are part of a node stack.
    union
    {
        struct
        {
            size_t height;
            AVLNode *owned_nodestack;   // if we have a nodestack, then this member points at it.
            AVLNode *left, *right;      // AVL structure default members.
        };
        struct
        {
            AVLNode *nodestack_owner;   // if nodestack member, then this points to the nodestack holder.
            AVLNode *prev, *next;       // double-linked list in case of node stack member.
        };
    };
};

// Should never be thrown, but who knows?
struct avl_runtime_exception
{};

template <typename dispatcherType>
struct AVLTree
{
    // This class is NOT THREAD-SAFE.

private:
    // Here is a list of things we want to query the dispatcherType for.
    static AINLINE eir::eCompResult compare_nodes( const AVLNode *node, const AVLNode *compareWith )
    {
        return dispatcherType::CompareNodes( node, compareWith );
    }
    // other (optional) methods:
    // - template <typename valueType> static eir::eCompResult CompareNodeWithValue( const AVLNode *left, const valueType& right )

public:
    inline AVLTree( void ) noexcept
    {
        this->root = nullptr;
    }

    inline AVLTree( const AVLTree& right ) = delete;
    inline AVLTree( AVLTree&& right ) noexcept
    {
        this->root = right.root;

        right.root = nullptr;
    }

    inline ~AVLTree( void )
    {
        // Cleanup is done by all the systems that created node memory themselves.
        // We do never allocate memory in this data structure anyway.
        return;
    }

    inline AVLTree& operator = ( const AVLTree& right ) = delete;

    // WARNING: a friendly reminder, that this operator trashes any previous tree nodes.
    // so make sure to empty the tree (dealloc nodes, etc) before moving to it!
    inline AVLTree& operator = ( AVLTree&& right ) noexcept
    {
        this->root = right.root;

        right.root = nullptr;

        return *this;
    }

private:
    static AINLINE size_t height_func( size_t leftHeight, size_t rightHeight )
    {
        // So that we can choose a specific implementation of max function.
        return std::max( leftHeight, rightHeight );
    }

    static AINLINE size_t calc_height_of_node( AVLNode *firstNode, AVLNode *secondNode )
    {
        // Calculate the maximum number of nodes that are on a path to the bottom in a binary tree.

        if ( firstNode == nullptr && secondNode == nullptr )
        {
            return 0;
        }

        size_t height_of_nodes_underneath;

        if ( firstNode == nullptr )
        {
            height_of_nodes_underneath = secondNode->height;
        }
        else if ( secondNode == nullptr )
        {
            height_of_nodes_underneath = firstNode->height;
        }
        else
        {
            height_of_nodes_underneath = height_func( firstNode->height, secondNode->height );
        }

        // We include the node that starts of our child.
        // THIS IS NOT THE ROOT NODE THAT IS INCLUDED.
        return ( 1 + height_of_nodes_underneath );
    }

    AINLINE AVLNode** get_node_src_ptr( AVLNode *parent, AVLNode *node )
    {
        AVLNode **nodePtr;

        if ( parent )
        {
            if ( parent->left == node )
            {
                nodePtr = &parent->left;
            }
            else if ( parent->right == node )
            {
                nodePtr = &parent->right;
            }
            else
            {
                throw avl_runtime_exception();
            }
        }
        else
        {
            if ( this->root != node )
            {
                throw avl_runtime_exception();
            }

            nodePtr = &this->root;
        }

        return nodePtr;
    }

    AINLINE void rotate( AVLNode *node, AVLNode *moveToUsNeighbor, bool leftTrueRightFalse )
    {
        // Both node and moveToUsNeighbor must not be nullptr.
        assert( node != nullptr );
        assert( moveToUsNeighbor != nullptr );

        // Set the new direct node which would be empty if the tree performed rotation.
        AVLNode *newDirectNodeChild;
        AVLNode *otherDirectChild;
        AVLNode *tuggedWithNode;

        if ( leftTrueRightFalse )
        {
            assert( node->right == moveToUsNeighbor );

            // If we move to the left, then the left subtree of our right child is bigger than us,
            // thus is becomes our right subtree.

            newDirectNodeChild = moveToUsNeighbor->left;

            node->right = newDirectNodeChild;

            // Do not forget to update the direct relationship!
            moveToUsNeighbor->left = node;

            otherDirectChild = node->left;

            tuggedWithNode = moveToUsNeighbor->right;
        }
        else
        {
            assert( node->left == moveToUsNeighbor );

            newDirectNodeChild = moveToUsNeighbor->right;

            node->left = newDirectNodeChild;

            moveToUsNeighbor->right = node;

            otherDirectChild = node->right;

            tuggedWithNode = moveToUsNeighbor->left;
        }

        // Update the parents.
        if ( newDirectNodeChild != nullptr )
        {
            newDirectNodeChild->parent = node;
        }

        AVLNode *prevParentOfMainNode = node->parent;

        moveToUsNeighbor->parent = prevParentOfMainNode;

        // Update the node pointer.
        {
            AVLNode **nodePtr = get_node_src_ptr( prevParentOfMainNode, node );

            *nodePtr = moveToUsNeighbor;
        }

        node->parent = moveToUsNeighbor;

        // Need to update node heights.
        size_t subHeight = calc_height_of_node( newDirectNodeChild, otherDirectChild );

        node->height = subHeight;

        // Height of the node on top.
        size_t fullHeight = 1;

        if ( tuggedWithNode == nullptr )
        {
            fullHeight += subHeight;
        }
        else
        {
            fullHeight += height_func( subHeight, tuggedWithNode->height );
        }

        moveToUsNeighbor->height = fullHeight;
    }

    static AINLINE bool detect_tree_two_imbalance( AVLNode *maybeBigger, AVLNode *maybeSmaller )
    {
        // Calculates the first imbalance equation of the AVL balance method,

        if ( maybeBigger == nullptr )
        {
            return false;
        }

        size_t maybeBiggerHeight = maybeBigger->height;

        if ( maybeSmaller == nullptr )
        {
            return ( maybeBiggerHeight > 0 );
        }

        size_t maybeSmallerHeight = maybeSmaller->height;

        return ( maybeBiggerHeight > ( 1 + maybeSmallerHeight ) );
    }

    static AINLINE bool detect_tree_one_imbalance( AVLNode *maybeSmaller, AVLNode *maybeBigger )
    {
        // Calculates the second imbalance equation of the AVL balance method,

        if ( maybeBigger == nullptr )
        {
            return false;
        }
        if ( maybeSmaller == nullptr )
        {
            return true;
        }

        return ( maybeSmaller->height < maybeBigger->height );
    }

    AINLINE void balance( AVLNode *node )
    {
        AVLNode *leftNode = node->left;
        AVLNode *rightNode = node->right;

        if ( leftNode != nullptr && detect_tree_two_imbalance( leftNode, rightNode ) )
        {
            AVLNode *rightNodeOfLeftNode = leftNode->right;

            if ( rightNodeOfLeftNode != nullptr && detect_tree_one_imbalance( leftNode->left, rightNodeOfLeftNode ) )
            {
                rotate( leftNode, rightNodeOfLeftNode, true );

                leftNode = rightNodeOfLeftNode;
            }

            rotate( node, leftNode, false );
        }
        else if ( rightNode != nullptr && detect_tree_two_imbalance( rightNode, leftNode ) )
        {
            AVLNode *leftNodeOfRightNode = rightNode->left;

            if ( leftNodeOfRightNode != nullptr && detect_tree_one_imbalance( rightNode->right, leftNodeOfRightNode ) )
            {
                rotate( rightNode, leftNodeOfRightNode, false );

                rightNode = leftNodeOfRightNode;
            }

            rotate( node, rightNode, true );
        }
    }

    AINLINE AVLNode* ScanForNodePosition( AVLNode *node, AVLNode**& iterPtrOut, AVLNode*& parentOut )
    {
        // Find the position to insert our new node at.
        AVLNode *parent = nullptr;
        AVLNode **iterPtr = &this->root;
        AVLNode *iter = *iterPtr;

        while ( true )
        {
            if ( iter == nullptr )
            {
                // Quit because we hit rock bottom.
                break;
            }

            AVLNode *newParent = iter;

            // Check key condition.
            eir::eCompResult cmpRes = compare_nodes( node, iter );

            if ( cmpRes == eir::eCompResult::LEFT_LESS )
            {
                iterPtr = &iter->left;
                iter = iter->left;
            }
            else if ( cmpRes == eir::eCompResult::LEFT_GREATER )
            {
                iterPtr = &iter->right;
                iter = iter->right;
            }
            else
            {
                // Quit because we found the node.
                break;
            }

            // Update the parent.
            parent = newParent;
        }

        iterPtrOut = iterPtr;
        parentOut = parent;
        return iter;
    }

    // Note that we assume here that we have a "nearly-correct" AVL tree, which means
    // that there is at max an error of one, a difference of 2 in height compared to
    // the other child subtree.
    AINLINE void update_invalidated_tree_height( AVLNode *parent )
    {
        while ( parent != nullptr )
        {
            parent->height = calc_height_of_node( parent->left, parent->right );

            balance( parent );

            parent = parent->parent;
        }
    }

public:
    // Note that we have no more failure case because our AVL tree implementation does support adding
    // multiple same values.
    // We do allow exceptions because the comparator could throw one.
    inline void Insert( AVLNode *node )
    {
        // Find the position to insert our new node at.
        AVLNode *parent;
        AVLNode **iterPtr;
        {
            AVLNode *iter = ScanForNodePosition( node, iterPtr, parent );

            if ( iter != nullptr )
            {
                // We already have found a node, so we have to make the new node part of the node stack!
                node->parent = node;
                node->nodestack_owner = iter;
                
                AVLNode *nodestack = iter->owned_nodestack;

                if ( nodestack == nullptr )
                {
                    LIST_CLEAR( *node );

                    iter->owned_nodestack = node;
                }
                else
                {
                    LIST_INSERT( *nodestack, *node );
                }

                return;
            }
        }

        // Just put the node here.
        *iterPtr = node;

        // Initialize the node properly.
        node->height = 0;
        node->left = nullptr;
        node->right = nullptr;
        node->parent = parent;
        node->owned_nodestack = nullptr;

        // Update to the very top all trees that have been modified.
        // This is much more efficient than doing a recursive method.
        update_invalidated_tree_height( parent );
    }

private:
    // Simple method that is compatible with any binary tree. :)
    static AINLINE AVLNode* FindMaxNode( AVLNode *searchFrom, AVLNode**& searchFromPtrInOut )
    {
        AVLNode *curMax = searchFrom;
        AVLNode **searchFromPtr = searchFromPtrInOut;

        while ( true )
        {
            AVLNode *nextBigger = curMax->right;

            if ( nextBigger == nullptr )
            {
                break;
            }

            searchFromPtr = &curMax->right;
            curMax = nextBigger;
        }

        searchFromPtrInOut = searchFromPtr;
        return curMax;
    }

    static AINLINE void remove_nodestack_member( AVLNode *node, AVLNode *nodestack_owner )
    {
        // Remove us from the nodestack.
        bool is_empty = LIST_EMPTY( *node );

        if ( is_empty )
        {
            // There is no more nodestack on the owner.
            assert( nodestack_owner != nullptr );
            assert( nodestack_owner == node->nodestack_owner );

            nodestack_owner->owned_nodestack = nullptr;
        }
        else
        {
            // Just remove us from the nodestack.
            if ( nodestack_owner->owned_nodestack == node )
            {
                nodestack_owner->owned_nodestack = node->next;
            }

            LIST_REMOVE( *node );
        }
    }
    
    static AINLINE void inplace_node_replace(
        size_t nodeHeight, AVLNode **nodePtr, AVLNode *leftChild, AVLNode *rightChild, AVLNode *parent, AVLNode *owned_nodestack,
        AVLNode *replaceBy
    )
    {
        // Replace nodes.
        replaceBy->parent = parent;
        replaceBy->left = leftChild;
        replaceBy->right = rightChild;
        replaceBy->height = nodeHeight;
        replaceBy->owned_nodestack = owned_nodestack;

        // If we took over a live nodestack, then we have to update the nodestack owners on the members.
        if ( owned_nodestack != nullptr )
        {
            AVLNode *curnode = owned_nodestack;

            do
            {
                curnode->nodestack_owner = replaceBy;

                curnode = curnode->next;
            }
            while ( curnode != owned_nodestack );
        }

        *nodePtr = replaceBy;

        // Update parents.
        if ( leftChild != nullptr )
        {
            leftChild->parent = replaceBy;
        }

        if ( rightChild != nullptr )
        {
            rightChild->parent = replaceBy;
        }
    }

    AINLINE void remove_node( AVLNode *node, AVLNode **nodePtr, AVLNode *leftChild, AVLNode *rightChild, AVLNode *parent )
    {
        // We assume that node is NOT part of a nodestack!
        assert( parent != node );

        // If we have a nodestack, then we just shedule the first member of the nodestack to be new member of the AVL tree.
        if ( AVLNode *new_node = node->owned_nodestack )
        {
            // Remove it from the nodestack.
            remove_nodestack_member( new_node, node );

            // Set it in-place of node.
            inplace_node_replace(
                node->height, nodePtr, leftChild, rightChild, parent, node->owned_nodestack,
                new_node
            );
        }
        else
        {
            if ( leftChild == nullptr )
            {
                *nodePtr = rightChild;

                if ( rightChild != nullptr )
                {
                    rightChild->parent = parent;
                }
            }
            else if ( rightChild == nullptr )
            {
                *nodePtr = leftChild;

                if ( leftChild != nullptr )
                {
                    leftChild->parent = parent;
                }
            }
            else
            {
                // Pretty complicated situation.
                AVLNode **closestReplacementPtr = &node->left;
                AVLNode *closestReplacement = FindMaxNode( leftChild, closestReplacementPtr );

                // Now we perform what someone usually has to do when updating the value of a node.
                // But this is optimized to keep the pointers valid.

                // Remove the closest node.
                // We know that the node has no right child, because it is the maximum in its subtree.
                // But since remove_node has become pretty complicated we rather stick to a call of itself :/
                remove_node( closestReplacement, closestReplacementPtr, closestReplacement->left, nullptr, closestReplacement->parent );

                // We need to update members because they could be out-of-date.
                parent = node->parent;
                leftChild = node->left;
                rightChild = node->right;

                nodePtr = get_node_src_ptr( parent, node );

                inplace_node_replace(
                    node->height, nodePtr, leftChild, rightChild, parent, nullptr,
                    closestReplacement
                );

                // Now closestReplacement takes the role of node.
            }

            // Update height of the changed tree.
            update_invalidated_tree_height( parent );
        }
    }

public:
    inline bool IsNodeInsideTree( AVLNode *node )
    {
        AVLNode *parent;
        AVLNode **iterPtr;

        AVLNode *iter = ScanForNodePosition( node, iterPtr, parent );

        if ( iter == nullptr )
        {
            return false;
        }

        // Check if it is either this node or part of the node stack.
        if ( iter != node )
        {
            if ( AVLNode *nodestack = iter->owned_nodestack )
            {
                AVLNode *curnode = nodestack;

                do
                {
                    if ( curnode == node )
                    {
                        return true;
                    }

                    curnode = curnode->next;
                }
                while ( curnode != nodestack );

                return false;
            }
        }

        return true;
    }

    inline void RemoveByNodeFast( AVLNode *node )
    {
        // A faster removal method which assumes that node does really reside inside this AVL tree.
        AVLNode *parent = node->parent;

        if ( parent == node )
        {
            remove_nodestack_member( node, node->nodestack_owner );
        }
        else
        {
            AVLNode **iterPtr = get_node_src_ptr( parent, node );

            remove_node( node, iterPtr, node->left, node->right, parent );
        }
    }

    inline bool RemoveByNode( AVLNode *node )
    {
        // Find the node we are talking about.
        // This is basically just verification that the node is inside this tree, meow.
        // Use RemoveByNodeFast if you know for sure that the node is inside this tree.
        AVLNode *parent;
        AVLNode **iterPtr;
        {
            AVLNode *iter = ScanForNodePosition( node, iterPtr, parent );

            if ( iter == nullptr )
            {
                // We could not find the node, so quit.
                return false;
            }

            if ( node != iter )
            {
                // It is not the same node!
                // It could still be part of its nodestack.
                if ( AVLNode *nodestack = iter->owned_nodestack )
                {
                    AVLNode *curnode = nodestack;

                    do
                    {
                        if ( curnode == node )
                        {
                            // Found it.
                            remove_nodestack_member( node, iter );

                            return true;
                        }

                        curnode = curnode->next;
                    }
                    while ( curnode != nodestack );
                }

                return false;
            }
        }

        remove_node( node, iterPtr, node->left, node->right, parent );

        return true;
    }

    // Iterates over every same-value node of a node-stack. The walkNode must be a node
    // that is linked to the AVL tree (not a node-stack member itself).
    struct nodestack_iterator
    {
        AINLINE nodestack_iterator( AVLNode *walkNode )
        {
            // Must not be part of node-stack.
            assert( walkNode->parent != walkNode );

            this->curNode = walkNode;
            this->nodestack = nullptr;
            this->doesIterateNodestack = false;
            this->isEnd = ( walkNode == nullptr );
        }
        AINLINE nodestack_iterator( const nodestack_iterator& ) = default;
        AINLINE ~nodestack_iterator( void ) = default;

        AINLINE nodestack_iterator& operator = ( const nodestack_iterator& ) = default;

        AINLINE bool IsEnd( void ) const
        {
            return this->isEnd;
        }

        AINLINE void Increment( void )
        {
            AVLNode *curNode = this->curNode;

            assert( curNode != nullptr );

            if ( this->doesIterateNodestack )
            {
                curNode = curNode->next;

                if ( curNode == this->nodestack )
                {
                    this->isEnd = true;
                }
            }
            else
            {
                AVLNode *nodestack = curNode->owned_nodestack;

                if ( nodestack != nullptr )
                {
                    curNode = nodestack;
                    this->nodestack = nodestack;
                    this->doesIterateNodestack = true;
                }
                else
                {
                    curNode = nullptr;
                    this->isEnd = true;
                }
            }

            // It has to change through iteration.
            // Very friendly to do so at the end aswell.
            this->curNode = curNode;
        }

        AINLINE AVLNode* Resolve( void )
        {
            return this->curNode;
        }

        AVLNode *curNode;
        AVLNode *nodestack;
        bool doesIterateNodestack;
        bool isEnd;
    };

    // Calls the function for each node-stack member of the node.
    // Should be used if you want to iterate over absolutely every node of a tree.
    template <typename callbackType>
    static AINLINE void call_for_each_node_for_nodestack( AVLNode *walkNode, const callbackType& cb )
    {
        nodestack_iterator iter( walkNode );

        while ( !iter.IsEnd() )
        {
            cb( iter.Resolve() );

            iter.Increment();
        }
    }

    // Iterates over each value-different node without counting the same-value nodestack members.
    struct diff_node_iterator
    {
        AINLINE diff_node_iterator( AVLNode *walkNode )
        {
            this->curNode = walkNode;
        }
        AINLINE diff_node_iterator( AVLTree& tree ) : diff_node_iterator( tree.GetSmallestNode() )
        {
            return;
        }
        AINLINE diff_node_iterator( const diff_node_iterator& right ) = default;
        AINLINE ~diff_node_iterator( void ) = default;

        AINLINE diff_node_iterator& operator = ( const diff_node_iterator& right ) = default;

        AINLINE bool IsEnd( void ) const
        {
            return ( this->curNode == nullptr );
        }

        AINLINE void Increment( void )
        {
            AVLNode *curNode = this->curNode;

            // Iterate the right.
            if ( AVLNode *rightNode = curNode->right )
            {
                curNode = rightNode;

                // Go to the first item of the newly-found subtree.
                while ( AVLNode *leftChild = curNode->left )
                {
                    curNode = leftChild;
                }
            }
            else
            {
                // Go up until we find a branch to the right again.
                // On this way up call for each node when we come from a left branch.
                AVLNode *parent = curNode->parent;

                while ( parent != nullptr && curNode != parent->left )
                {
                    // Go up one more.
                    curNode = parent;
                    parent = parent->parent;
                }

                curNode = parent;
            }

            this->curNode = curNode;
        }

        AINLINE AVLNode* Resolve( void )
        {
            return this->curNode;
        }

    private:
        AVLNode *curNode;
    };

    // Goes through each node in sorted order.
    // This is just a helper for the entire tree.
    template <typename callbackType>
    inline void Walk( const callbackType& cb )
    {
        diff_node_iterator iter( this->GetSmallestNode() );

        while ( !iter.IsEnd() )
        {
            // First iterate the left child, then current node and then the right node,
            // just like in a regular binary tree.

            // Now we can iterate this node.
            call_for_each_node_for_nodestack( iter.Resolve(), cb );

            iter.Increment();
        }
    }

    // Returns the maximum path length to the botton of the tree.
    inline size_t GetTreeHeight( void ) const
    {
        if ( AVLNode *rootNode = this->root )
        {
            return ( 1 + rootNode->height );
        }

        return 0;
    }

    // Returns the smallest node that is part of the tree.
    inline AVLNode* GetSmallestNode( void )
    {
        AVLNode *curNode = this->root;

        if ( !curNode )
        {
            return nullptr;
        }

        while ( AVLNode *smallerNode = curNode->left )
        {
            curNode = smallerNode;
        }

        return curNode;
    }

    // Returns the biggest node that is linked into the tree.
    inline AVLNode* GetBiggestNode( void )
    {
        AVLNode *curNode = this->root;

        if ( !curNode )
        {
            return nullptr;
        }

        while ( AVLNode *biggerNode = curNode->right )
        {
            curNode = biggerNode;
        }

        return curNode;
    }

    // Returns the node at the top of the tree.
    inline AVLNode* GetRootNode( void )
    {
        return this->root;
    }
    inline const AVLNode* GetRootNode( void ) const
    {
        return this->root;
    }

    // Returns the node whose value is just above or equal to the given value.
    template <typename valueType>
    inline AVLNode* GetJustAboveOrEqualNode( const valueType& theValue )
    {
        AVLNode *closestAboveNode = nullptr;

        // Search for the equal node.
        // On the way of search check all that are bigger or equal to theValue.
        AVLNode *walkNode = this->root;

        while ( walkNode )
        {
            // Walk to the next node.
            eir::eCompResult cmpRes = dispatcherType::CompareNodeWithValue( walkNode, theValue );

            if ( cmpRes == eir::eCompResult::LEFT_LESS )
            {
                walkNode = walkNode->right;
            }
            else if ( cmpRes == eir::eCompResult::EQUAL )
            {
                // We found the exact node. There is no beating that.
                return walkNode;
            }
            else
            {
                // We have found a closer node to our accoplishment.
                closestAboveNode = walkNode;

                // Any further node we find must be closer to theValue because it is left from the closest.
                walkNode = walkNode->left;
            }
        }

        return closestAboveNode;
    }

    // Returns the node that matches the given value.
    template <typename customDispatcherType, typename valueType>
    inline AVLNode* FindNodeCustom( const valueType& value )
    {
        AVLNode *curNode = this->root;

        while ( curNode )
        {
            // Check which one to go for next.
            eir::eCompResult cmpRes = customDispatcherType::CompareNodeWithValue( curNode, value );

            if ( cmpRes == eir::eCompResult::LEFT_LESS )
            {
                curNode = curNode->right;
            }
            else if ( cmpRes == eir::eCompResult::LEFT_GREATER )
            {
                curNode = curNode->left;
            }
            else
            {
                // We found it.
                break;
            }
        }

        return curNode;
    }
    template <typename valueType>
    inline AVLNode* FindNode( const valueType& value ) { return FindNodeCustom <dispatcherType> ( value ); }

    // Returns the node that matches the given value, const-equivalent.
    template <typename customDispatcherType, typename valueType>
    inline const AVLNode* FindNodeCustom( const valueType& value ) const
    {
        const AVLNode *curNode = this->root;

        while ( curNode )
        {
            // Check which one to go for next.
            eir::eCompResult cmpRes = customDispatcherType::CompareNodeWithValue( curNode, value );

            if ( cmpRes == eir::eCompResult::LEFT_LESS )
            {
                curNode = curNode->right;
            }
            else if ( cmpRes == eir::eCompResult::LEFT_GREATER )
            {
                curNode = curNode->left;
            }
            else
            {
                // We found it.
                break;
            }
        }

        return curNode;
    }
    template <typename valueType>
    inline const AVLNode* FindNode( const valueType& value ) const { return FindNodeCustom <dispatcherType> ( value ); }

    // Returns any node that matches our criteria.
    template <typename callbackType>
    AINLINE AVLNode* FindAnyNodeByCriteria( const callbackType& cb )
    {
        AVLNode *curNode = this->root;

        while ( curNode )
        {
            eir::eCompResult cmpRes = cb( curNode );

            if ( cmpRes == eir::eCompResult::LEFT_LESS )
            {
                curNode = curNode->right;
            }
            else if ( cmpRes == eir::eCompResult::LEFT_GREATER )
            {
                curNode = curNode->left;
            }
            else
            {
                // Found any node.
                break;
            }
        }

        return curNode;
    }

    // Finds the minimum node that matches certain criteria.
    // Returning eCompResult::EQUAL indicates a match but in this function multiple nodes are allowed to be equal, forming
    // classes of values.
    // IMPORTANT: node classes are supposed to be contiguous! meaning there cannot be a node not in the class between two
    // nodes that instead are.
    template <typename callbackType>
    inline AVLNode* FindMinimumNodeByCriteria( const callbackType& cb )
    {
        // NOTE: cb is a virtual "right value-span". it is pretty complicated but just roll with it.

        // We first try to find any node in the class.
        AVLNode *curNode = FindAnyNodeByCriteria( cb );

        if ( curNode != nullptr )
        {
            // Now walk until we reach the smallest.
            while ( AVLNode *leftNode = curNode->left )
            {
                eir::eCompResult cmpRes = cb( leftNode );

                if ( cmpRes == eir::eCompResult::EQUAL )
                {
                    // Still in the class.
                    curNode = leftNode;
                }
                else
                {
                    assert( cmpRes == eir::eCompResult::LEFT_LESS );
                    break;
                }
            }
        }

        return curNode;
    }

    template <typename callbackType>
    inline AVLNode* FindMaximumNodeByCriteria( const callbackType& cb )
    {
        AVLNode *curNode = FindAnyNodeByCriteria( cb );

        if ( curNode != nullptr )
        {
            while ( AVLNode *rightNode = curNode->right )
            {
                eir::eCompResult cmpRes = cb( rightNode );

                if ( cmpRes == eir::eCompResult::EQUAL )
                {
                    curNode = rightNode;
                }
                else
                {
                    assert( cmpRes == eir::eCompResult::LEFT_GREATER );
                    break;
                }
            }
        }

        return curNode;
    }

    // Should always succeed.
    inline void Validate( void )
    {
        Walk(
            []( AVLNode *node )
        {
            // Ignore any nodestack member.
            AVLNode *parent = node->parent;

            if ( parent == node )
            {
                return;
            }

            if ( parent )
            {
                if ( parent->left == node )
                {
                    assert( compare_nodes( node, parent ) == eir::eCompResult::LEFT_LESS );
                }
                else if ( parent->right == node )
                {
                    assert( compare_nodes( parent, node ) == eir::eCompResult::LEFT_LESS );
                }
                else
                {
                    assert( 0 );
                }
            }

            if ( AVLNode *leftNode = node->left )
            {
                assert( leftNode->parent == node );
                assert( compare_nodes( leftNode, node ) == eir::eCompResult::LEFT_LESS );
            }

            if ( AVLNode *rightNode = node->right )
            {
                assert( rightNode->parent == node );
                assert( compare_nodes( node, rightNode ) == eir::eCompResult::LEFT_LESS );
            }

            // TODO: verify the height.
        });
    }


private:
    AVLNode *root;
};

#endif //_AVL_TREE_IMPLEMENTATION_