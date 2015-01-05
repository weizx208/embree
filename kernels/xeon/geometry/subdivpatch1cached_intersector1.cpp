// ======================================================================== //
// Copyright 2009-2014 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include "subdivpatch1cached_intersector1.h"
#include "xeon/bvh4/bvh4.h"
#include "xeon/bvh4/bvh4_intersector1.h"

#define TIMER(x)
#define DBG(x)

//MUST COPY FROM L2 -> L1, otherwise L2 eviction won't evict L1
  

namespace embree
{
  namespace isa
  {  
    
    __thread TESSELLATION_CACHE *SubdivPatch1CachedIntersector1::thread_cache = NULL;
    
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /*! Returns BVH4 node reference for subtree over patch grid */
    size_t SubdivPatch1CachedIntersector1::getSubtreeRootNode(Precalculations& pre, SharedTessellationCache &shared_cache, const SubdivPatch1Cached* const subdiv_patch, const void* geom)
    {
      DBG(DBG_PRINT(pre.rw_mtx));

#if defined(SHARED_TESSELLATION_CACHE)      
      if (likely(pre.rw_mtx != NULL))
        {
          DBG(DBG_PRINT("READ_UNLOCK"));          
          pre.rw_mtx->read_unlock();
          DBG(DBG_PRINT(pre.rw_mtx));
        }
#endif
      
      const unsigned int commitCounter = ((Scene*)geom)->commitCounter;
      InputTagType tag = (InputTagType)subdiv_patch;

      DBG(DBG_PRINT(tag));

      SharedTessellationCache::CacheTag *t = shared_cache.getTag(tag);
      /* read lock */
      DBG(DBG_PRINT("READ_LOCK"));
      t->read_lock();
      DBG(DBG_PRINT(&t->mtx));
      
#if defined(SHARED_TESSELLATION_CACHE)      
      /* remember read/write mutex */
      pre.rw_mtx = &t->mtx;
#endif      

      /* patch data not in cache? */
      CACHE_STATS(SharedTessellationCache::cache_accesses++);
      
      if (unlikely(!t->match(tag,commitCounter)))
        {
          CACHE_STATS(SharedTessellationCache::cache_misses++);
          
          DBG(DBG_PRINT("CACHE_MISS"));                
          
          /* read unlock */
          DBG(DBG_PRINT("READ_UNLOCK"));                          
          t->read_unlock();
          DBG(DBG_PRINT(&t->mtx));
          

          DBG(DBG_PRINT("PREFETCH"));                

          subdiv_patch->prefetchData();

          /* write lock */
          DBG(DBG_PRINT("WRITE LOCK"));                          
          t->write_lock();
          DBG(DBG_PRINT(&t->mtx));

 
          /* update */
          const unsigned int needed_blocks = subdiv_patch->grid_subtree_size_64b_blocks;
          DBG(DBG_PRINT(needed_blocks));                          
          
          BVH4::Node* node = (BVH4::Node*)t->getPtr();
          if (t->blocks() < needed_blocks)
            {
              DBG(DBG_PRINT("EXPAND DATA MEM"));                          
              
              if (node != NULL)
                {
                  DBG(DBG_PRINT("FREE PREVIOUS DATA MEM"));                                                              
                  free_tessellation_cache_mem(node);                 
                }
                

              DBG(DBG_PRINT("ALLOCATE PREVIOUS DATA MEM"));                                            
              node = (BVH4::Node*)alloc_tessellation_cache_mem(needed_blocks);
              
              CACHE_STATS(SharedTessellationCache::cache_evictions++);              

              t->set(tag,commitCounter,0,needed_blocks);
              t->setPtr(node);
            }
          else
            {
              t->update(tag,commitCounter);
              
              assert(node != NULL);
            }

          DBG(DBG_PRINT("BUILD SUB TREE"));                                            
          
          size_t new_root = (size_t)buildSubdivPatchTree(*subdiv_patch,node,((Scene*)geom)->getSubdivMesh(subdiv_patch->geom));

          
          assert( new_root != BVH4::invalidNode);
          
          SharedTessellationCache::updateRootRef(*t,new_root);

          /* write unlock and read lock */
          DBG(DBG_PRINT("WRITE UNLOCK READ LOCK"));                                                      
          t->write_unlock_set_read_lock();
          DBG(DBG_PRINT(&t->mtx));

          DBG(DBG_PRINT(new_root));                                            
          DBG(shared_cache.print());
          
          return new_root;
        }
      CACHE_STATS(SharedTessellationCache::cache_hits++);              
      
      DBG(DBG_PRINT("CACHE HIT"));                                            

      BVH4::NodeRef root = t->getRef();
      DBG(DBG_PRINT(root));
      DBG(shared_cache.print());
      
      return root;
    }

    void updateNodeRefs(size_t &nr, const size_t old_ptr, const size_t new_ptr)
    {
      PING;
      BVH4::NodeRef ref(nr);
      
      if (unlikely(ref == BVH4::emptyNode))
        return;

      assert(ref != BVH4::invalidNode);

      /* this is a leaf node */
      if (unlikely(ref.isLeaf()))
        {
          nr = nr - old_ptr + new_ptr;
          assert( BVH4::NodeRef(nr).isLeaf() );
          return;
        }

      BVH4::Node* node = ref.node();
      
      for (size_t i=0;i<4;i++)
        if (node->child(i) != BVH4::emptyNode)
          updateNodeRefs(*(size_t*)&node->child(i),old_ptr,new_ptr);

      nr = nr - old_ptr + new_ptr;                  
    }
    
    size_t SubdivPatch1CachedIntersector1::getSubtreeRootNodeFromCacheHierarchy(Precalculations& pre,
                                                                                SharedTessellationCache &shared_cache,
                                                                                const SubdivPatch1Cached* const subdiv_patch,
                                                                                const void* geom)
    {
      DBG(DBG_PRINT(pre.local_cache));                                            
    
      const unsigned int commitCounter = ((Scene*)geom)->commitCounter;
      InputTagType tag = (InputTagType)subdiv_patch;

      DBG(DBG_PRINT((size_t)tag / 320));
      BVH4::NodeRef root = pre.local_cache->lookup(tag,commitCounter);
      root.prefetch(0);
      if (unlikely(root == (size_t)-1)) /* L1 cache miss ? */
        {
          DBG(DBG_PRINT("L1 CACHE MISS"));                                            

          /* is data in L2 */
          SharedTessellationCache::CacheTag *t_l2 = shared_cache.getTag(tag);
          t_l2->read_lock();
          CACHE_STATS(SharedTessellationCache::cache_accesses++);
      
          if (1 || unlikely(!t_l2->match(tag,commitCounter))) /* not in L2 either */
            {
              DBG(DBG_PRINT("L2 CACHE MISS"));                                            
              
              subdiv_patch->prefetchData();

              CACHE_STATS(SharedTessellationCache::cache_misses++);
              t_l2->read_unlock();
              t_l2->write_lock();
              /* update */
              const unsigned int needed_blocks = subdiv_patch->grid_subtree_size_64b_blocks;
              DBG(DBG_PRINT(needed_blocks));                          
          
              if (1 || t_l2->blocks() < needed_blocks)
                {
                  DBG(DBG_PRINT("EXPAND L2 CACHE ENTRY"));                                            
                  BVH4::Node* node = (BVH4::Node*)t_l2->getPtr();
                  
                  if (node != NULL)
                    free_tessellation_cache_mem(node);                                 

                  node = (BVH4::Node*)alloc_tessellation_cache_mem(needed_blocks);
                  DBG(DBG_PRINT(node));
              
                  CACHE_STATS(SharedTessellationCache::cache_evictions++);              

                  t_l2->set(tag,commitCounter,0,needed_blocks);
                  t_l2->setPtr(node);
                  assert(t_l2->getPtr() == node);
                }
              else
                {
                  DBG(DBG_PRINT("REUSE L2 CACHE ENTRY"));                                                              
                  t_l2->update(tag,commitCounter);              
                  assert(t_l2->getPtr() != NULL);
                }
              
              BVH4::Node* node = (BVH4::Node*)t_l2->getPtr();
              DBG(DBG_PRINT(node));
             
              size_t new_root = (size_t)buildSubdivPatchTree(*subdiv_patch,node,((Scene*)geom)->getSubdivMesh(subdiv_patch->geom));
              DBG(DBG_PRINT(new_root));
              
              assert( new_root != BVH4::invalidNode);          
              SharedTessellationCache::updateRootRef(*t_l2,new_root);
              assert(t_l2->getRef() == new_root);
              
              /* get L1 cache tag to evict */
              TESSELLATION_CACHE::CacheTag &t_l1 = pre.local_cache->request_LRU(tag,commitCounter); //,needed_blocks);
             
              /* copy data to L1 */
              DBG(DBG_PRINT("INIT FROM SHARED CACHE TAG"));                                                              

#if 1
              t_l1.copyFromSharedCacheTag(*t_l2);
#else
              t_l1.initFromSharedCacheTag(*t_l2);
              DBG(DBG_PRINT("MEMCPY"));                                                              
              memcpy(t_l1.ptr,t_l2->ptr,t_l2->blocks()*64);
              DBG(DBG_PRINT("updateNodeRefs"));                                                              
              DBG(DBG_PRINT(t_l2->ptr));
              DBG(DBG_PRINT(t_l1.ptr));
              
              updateNodeRefs(t_l1.getRootRef(),(size_t)t_l2->ptr,(size_t)t_l1.ptr);
#endif
              DBG(t_l2->print());
              DBG(t_l1.print());
              
              /* write unlock */
              t_l2->write_unlock();

              DBG(pre.local_cache->print());
              
              /* return data from L1 */
              assert( pre.local_cache->lookup(tag,commitCounter) != (size_t)-1 );
              BVH4::NodeRef l1_root = t_l1.getRootRef();
              DBG(DBG_PRINT(l1_root));
              assert(l1_root == new_root);
              return l1_root;          

            }
          else
            {
              DBG(DBG_PRINT("L2 CACHE HIT"));                                            
              FATAL("HERE2");
              DBG(pre.local_cache->print());
              
              CACHE_STATS(SharedTessellationCache::cache_hits++);
              TESSELLATION_CACHE::CacheTag &t_l1 = pre.local_cache->request(tag,commitCounter,t_l2->blocks());

              DBG(t_l1.print());
              
              /* copy data to L1 */
              DBG(DBG_PRINT("INIT FROM SHARED CACHE TAG"));                                                                            
              t_l1.initFromSharedCacheTag(*t_l2);
              DBG(DBG_PRINT("MEMCPY"));                                                                            
              memcpy(t_l1.ptr,t_l2->ptr,t_l2->blocks()*64);
              DBG(DBG_PRINT("updateNodeRefs"));                                                                            
              updateNodeRefs(t_l1.getRootRef(),(size_t)t_l2->ptr,(size_t)t_l1.ptr);
              
              t_l2->read_unlock();

              /* return data from L1 */
              assert( pre.local_cache->lookup(tag,commitCounter) != (size_t)-1 );
              BVH4::NodeRef l1_root = t_l1.getRootRef();
              return l1_root;                        
            }
        }
      DBG(DBG_PRINT("L1 CACHE HIT"));                                            
      return root;          
    }
    

    BVH4::NodeRef SubdivPatch1CachedIntersector1::buildSubdivPatchTree(const SubdivPatch1Cached &patch,
                                                                       void *const lazymem,
                                                                       const SubdivMesh* const geom)
    {      
      TIMER(double msec = 0.0);
      TIMER(msec = getSeconds());
        
      assert( patch.grid_size_simd_blocks >= 1 );
#if !defined(_MSC_VER) || defined(__INTEL_COMPILER)
      __aligned(64) float grid_x[(patch.grid_size_simd_blocks+1)*8]; 
      __aligned(64) float grid_y[(patch.grid_size_simd_blocks+1)*8];
      __aligned(64) float grid_z[(patch.grid_size_simd_blocks+1)*8]; 
        
      __aligned(64) float grid_u[(patch.grid_size_simd_blocks+1)*8]; 
      __aligned(64) float grid_v[(patch.grid_size_simd_blocks+1)*8];
     
#else
      const size_t array_elements = (patch.grid_size_simd_blocks + 1) * 8;
      float *const ptr = (float*)_malloca(5 * array_elements * sizeof(float) + 64);
      float *const grid_arrays = (float*)ALIGN_PTR(ptr,64);

      float *grid_x = &grid_arrays[array_elements * 0];
      float *grid_y = &grid_arrays[array_elements * 1];
      float *grid_z = &grid_arrays[array_elements * 2];
      float *grid_u = &grid_arrays[array_elements * 3];
      float *grid_v = &grid_arrays[array_elements * 4];

        
#endif   
      evalGrid(patch,grid_x,grid_y,grid_z,grid_u,grid_v,geom);
        
      BVH4::NodeRef subtree_root = BVH4::encodeNode( (BVH4::Node*)lazymem);
      unsigned int currentIndex = 0;
      BBox3fa bounds = createSubTree( subtree_root,
				      (float*)lazymem,
				      patch,
				      grid_x,
				      grid_y,
				      grid_z,
				      grid_u,
				      grid_v,
				      GridRange(0,patch.grid_u_res-1,0,patch.grid_v_res-1),
				      currentIndex,
				      geom);
        
      assert(currentIndex == patch.grid_subtree_size_64b_blocks);

      TIMER(msec = getSeconds()-msec);            

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
      _freea(ptr);
#endif
      return subtree_root;
    }
    
    
    BBox3fa SubdivPatch1CachedIntersector1::createSubTree(BVH4::NodeRef &curNode,
                                                          float *const lazymem,
                                                          const SubdivPatch1Cached &patch,
                                                          const float *const grid_x_array,
                                                          const float *const grid_y_array,
                                                          const float *const grid_z_array,
                                                          const float *const grid_u_array,
                                                          const float *const grid_v_array,
                                                          const GridRange &range,
                                                          unsigned int &localCounter,
                                                          const SubdivMesh* const geom)
    {
      if (range.hasLeafSize())
	{
	  const unsigned int u_start = range.u_start;
	  const unsigned int u_end   = range.u_end;
	  const unsigned int v_start = range.v_start;
	  const unsigned int v_end   = range.v_end;
        
	  const unsigned int u_size = u_end-u_start+1;
	  const unsigned int v_size = v_end-v_start+1;
        
	  assert(u_size >= 1);
	  assert(v_size >= 1);
        
	  assert(u_size*v_size <= 9);
        
	  const unsigned int currentIndex = localCounter;
	  localCounter +=  (sizeof(Quad2x2)+63) / 64; 
        
	  Quad2x2 *qquad = (Quad2x2*)&lazymem[currentIndex*16];
                
	  ssef leaf_x_array[3];
	  ssef leaf_y_array[3];
	  ssef leaf_z_array[3];
	  ssef leaf_u_array[3];
	  ssef leaf_v_array[3];
        
	  for (unsigned int v=v_start;v<=v_end;v++)
	    {
	      const size_t offset = v * patch.grid_u_res + u_start;
	      const unsigned int local_v = v - v_start;
	      leaf_x_array[local_v] = loadu4f(&grid_x_array[ offset ]);
	      leaf_y_array[local_v] = loadu4f(&grid_y_array[ offset ]);
	      leaf_z_array[local_v] = loadu4f(&grid_z_array[ offset ]);
	      leaf_u_array[local_v] = loadu4f(&grid_u_array[ offset ]);
	      leaf_v_array[local_v] = loadu4f(&grid_v_array[ offset ]);            
	    }
        
	  /* set invalid grid u,v value to border elements */
	  for (unsigned int x=u_size-1;x<3;x++)
	    for (unsigned int y=0;y<3;y++)
	      {
		leaf_x_array[y][x] = leaf_x_array[y][u_size-1];
		leaf_y_array[y][x] = leaf_y_array[y][u_size-1];
		leaf_z_array[y][x] = leaf_z_array[y][u_size-1];
		leaf_u_array[y][x] = leaf_u_array[y][u_size-1];
		leaf_v_array[y][x] = leaf_v_array[y][u_size-1];
	      }
        
	  for (unsigned int y=v_size-1;y<3;y++)
	    for (unsigned int x=0;x<3;x++)
	      {
		leaf_x_array[y][x] = leaf_x_array[v_size-1][x];
		leaf_y_array[y][x] = leaf_y_array[v_size-1][x];
		leaf_z_array[y][x] = leaf_z_array[v_size-1][x];
		leaf_u_array[y][x] = leaf_u_array[v_size-1][x];
		leaf_v_array[y][x] = leaf_v_array[v_size-1][x];
	      }
        
                
	  qquad->init( leaf_x_array, 
		       leaf_y_array, 
		       leaf_z_array, 
		       leaf_u_array, 
		       leaf_v_array);
        
#if 0
	  DBG_PRINT("LEAF");
	  DBG_PRINT(u_start);
	  DBG_PRINT(v_start);
	  DBG_PRINT(u_end);
	  DBG_PRINT(v_end);
        
	  for (unsigned int y=0;y<3;y++)
	    for (unsigned int x=0;x<3;x++)
	      std::cout << y << " " << x 
			<< " ->  x = " << leaf_x_array[y][x] << " y = " << leaf_v_array[y][x] << " z = " << leaf_z_array[y][x]
			<< "   u = " << leaf_u_array[y][x] << " v = " << leaf_v_array[y][x] << std::endl;
        
	  DBG_PRINT( *qquad );
        
#endif          
        
	  BBox3fa bounds = qquad->bounds();
	  curNode = BVH4::encodeLeaf(qquad,2);
        
	  return bounds;
	}
      
      
      /* allocate new bvh4 node */
      const size_t currentIndex = localCounter;
      
      /* 128 bytes == 2 x 64 bytes cachelines */
      localCounter += 2; 
      
      BVH4::Node *node = (BVH4::Node *)&lazymem[currentIndex*16];
      
      curNode = BVH4::encodeNode( node );
      
      node->clear();
      
      GridRange r[4];
      
      const unsigned int children = range.splitIntoSubRanges(r);
      
      /* create four subtrees */
      BBox3fa bounds( empty );
      
      for (unsigned int i=0;i<children;i++)
	{
	  BBox3fa bounds_subtree = createSubTree( node->child(i), 
						  lazymem, 
						  patch, 
						  grid_x_array,
						  grid_y_array,
						  grid_z_array,
						  grid_u_array,
						  grid_v_array,
						  r[i],						  
						  localCounter,
						  geom);
	  node->set(i, bounds_subtree);
	  bounds.extend( bounds_subtree );
	}
      
      return bounds;
    }
    
    void SubdivPatch1CachedIntersector1::createTessellationCache()
    {
      TESSELLATION_CACHE *cache = (TESSELLATION_CACHE *)_mm_malloc(sizeof(TESSELLATION_CACHE),64);
      assert( (size_t)cache % 64 == 0 );
      cache->init();	
#if defined(DEBUG) && 0
      static AtomicMutex mtx;
      mtx.lock();
      std::cout << "Enabling tessellation cache with " << cache->allocated64ByteBlocks() << " blocks = " << cache->allocated64ByteBlocks()*64 << " bytes as default size" << std::endl;
      mtx.unlock();
#endif
      thread_cache = cache;
    }
    
  };
}
