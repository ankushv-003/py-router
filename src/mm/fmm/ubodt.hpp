/**
 * Fast map matching.
 *
 * Upperbounded origin destination table
 *
 * @author: Can Yang
 * @version: 2020.01.31
 */

#ifndef FASTMM_UBODT_H_
#define FASTMM_UBODT_H_

#include "network/type.hpp"
#include "mm/mm_type.hpp"
#include "mm/transition_graph.hpp"
#include "util/debug.hpp"
#include <unordered_map>

namespace FASTMM
{
  namespace MM
  {

    /**
     * %Record type of the upper bounded origin destination table
     */
    struct Record
    {
      NETWORK::NodeIndex source;  /**< source node*/
      NETWORK::NodeIndex target;  /**< target node*/
      NETWORK::NodeIndex first_n; /**< next node visited from source to target */
      NETWORK::NodeIndex prev_n;  /**< last node visited before target */
      NETWORK::EdgeIndex next_e;  /**< next edge visited from source to target */
      double cost;                /**< distance from source to target */
    };

    /**
     * Upperbounded origin destination table using perfect hash function
     */
    class UBODT
    {
    public:
      UBODT(const UBODT &) = delete;
      UBODT &operator=(const UBODT &) = delete;
      /**
       * Construct a UBODT with metadata
       * @param num_vertices Number of vertices in the network (for perfect hashing)
       * @param delta Delta value used for generation
       * @param network_hash Hash of the network structure
       * @param mode Transition mode (0=SHORTEST, 1=FASTEST, -1=unknown)
       */
      UBODT(int num_vertices, double delta, const std::string &network_hash, TransitionMode mode);
      ~UBODT() = default;
      /**
       * Look up the row according to a source node and a target node
       * @param  source source node
       * @param  target target node
       * @return  A pointer to record if found, otherwise nullptr
       */
      const Record *look_up(NETWORK::NodeIndex source, NETWORK::NodeIndex target) const;

      /**
       * Look up a shortest path (SP) containing edges from source to target.
       * In case that SP is not found, empty is returned.
       * @param  source source node
       * @param  target target node
       * @return  a shortest path connecting source to target
       */
      std::vector<NETWORK::EdgeIndex> look_sp_path(NETWORK::NodeIndex source,
                                                   NETWORK::NodeIndex target) const;

      /**
       * Construct the complete path (a vector of edge ID) from an optimal path
       * (a vector of optimal nodes in the transition graph)
       *
       * @param path an optimal path
       * @param edges a vector of edges
       * @param indices the index of each optimal edge in the complete path
       * @param reverse_tolerance reverse tolerance parameter (in map units)
       * @return a complete path (topologically connected).
       * If there is a large gap in the optimal
       * path implying complete path cannot be found in UBDOT,
       * an empty path is returned
       */
      CompletePath construct_complete_path(const TGOpath &path,
                                           const std::vector<NETWORK::Edge> &edges,
                                           std::vector<int> *indices,
                                           double reverse_tolerance) const;
      /**
       * Get the upperbound of the UBODT
       * @return upperbound value
       */
      double get_delta() const;

      /**
       * Get the network hash this UBODT was generated from
       * @return network hash string (empty if old format file)
       */
      std::string get_network_hash() const;

      /**
       * Get the transition mode this UBODT was generated with
       * @return mode as TransitionMode enum
       */
      TransitionMode get_mode() const;

      /**
       * Get the number of vertices used for perfect hashing
       * @return number of vertices
       */
      int get_num_vertices() const;

      /**
       *  Insert a record into the hash table
       * @param r a record to be inserted
       */
      void insert(const Record &r);

      inline long long get_num_rows() const
      {
        return num_rows;
      };

      /**
       * Read UBODT from a binary file
       * @param  filename       input file name
       * @param  progress_step  print progress every N rows (default: 100000, 0 = no progress)
       * @return  A shared pointer to the UBODT data with metadata properties set
       */
      static std::shared_ptr<UBODT> read_ubodt(const std::string &filename, int progress_step = 100000);

    private:
      /**
       * Compute perfect hash for (source, target) pair
       * @param source Source node index
       * @param target Target node index
       * @return Unique hash value
       */
      inline uint64_t compute_hash(NETWORK::NodeIndex source, NETWORK::NodeIndex target) const
      {
        return (uint64_t)source * num_vertices + target;
      }

      const int num_vertices;                     // Number of vertices for perfect hashing
      std::unordered_map<uint64_t, Record> table; // Hash table using perfect hash
      long long num_rows = 0;
      const double delta;             // Delta value from generation
      const std::string network_hash; // hash of network structure
      const TransitionMode mode;      // transition mode (SHORTEST, FASTEST, etc)
    };
  }
}

#endif
