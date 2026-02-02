//
// Created by Can Yang on 2020/3/22.
//

#include "mm/fmm/ubodt.hpp"
#include "util/util.hpp"

#include <fstream>
#include <stdexcept>

#ifdef BOOST_OS_WINDOWS
#include <boost/throw_exception.hpp>
#endif
#include <boost/format.hpp>
#include <boost/archive/binary_iarchive.hpp>

using namespace FASTMM;
using namespace FASTMM::CORE;
using namespace FASTMM::NETWORK;
using namespace FASTMM::MM;
UBODT::UBODT(int num_vertices, double delta, const std::string &network_hash, TransitionMode mode)
    : num_vertices(num_vertices), delta(delta), network_hash(network_hash), mode(mode)
{
  SPDLOG_TRACE("Intialization UBODT with num_vertices={}, delta={}, hash={}, mode={}",
               num_vertices, delta, network_hash, static_cast<int>(mode));
  SPDLOG_TRACE("Intialization UBODT finished");
}

const Record *UBODT::look_up(NodeIndex source, NodeIndex target) const
{
  uint64_t hash = compute_hash(source, target);
  auto it = table.find(hash);
  if (it != table.end())
  {
    return &it->second;
  }
  return nullptr;
}

std::vector<EdgeIndex> UBODT::look_sp_path(NodeIndex source,
                                           NodeIndex target) const
{
  std::vector<EdgeIndex> edges;
  if (source == target)
  {
    return edges;
  }
  const Record *r = look_up(source, target);
  // No transition exist from source to target
  if (r == nullptr)
    if (r == nullptr)
    {
      return edges;
    }
  while (r->first_n != target)
  {
    edges.push_back(r->next_e);
    r = look_up(r->first_n, target);
    if (!r)
      break;
  }
  if (r)
    edges.push_back(r->next_e);
  return edges;
}

CompletePath UBODT::construct_complete_path(const TGOpath &path,
                                            const std::vector<Edge> &edges,
                                            std::vector<int> *indices,
                                            double reverse_tolerance) const
{
  CompletePath complete_path;
  if (!indices->empty())
    indices->clear();
  if (path.empty())
    return complete_path;
  int N = path.size();
  complete_path.push_back(path[0]->c->edge->id);
  int current_idx = 0;
  indices->push_back(current_idx);
  SPDLOG_TRACE("Insert index {}", current_idx);
  for (int i = 0; i < N - 1; ++i)
  {
    const Candidate *a = path[i]->c;
    const Candidate *b = path[i + 1]->c;

    SPDLOG_DEBUG("Check point {} a {} b {}", i, a->edge->id, b->edge->id);
    if ((a->edge->id != b->edge->id) || (a->offset - b->offset > reverse_tolerance))
    {
      // segs stores edge index
      auto segs = look_sp_path(a->edge->target, b->edge->source);
      // No transition exist in UBODT
      if (segs.empty() && a->edge->target != b->edge->source)
      {
        SPDLOG_DEBUG("Edges not found connecting a b");
        SPDLOG_DEBUG("reverse movement {} tolerance {}",
                     a->offset - b->offset, reverse_tolerance);
        SPDLOG_WARN("Traj unmatched as edge {} L {} offset {}"
                    " and edge {} L {} offset {} disconnected",
                    a->edge->id, a->edge->length, a->offset,
                    b->edge->id, b->edge->length, b->offset);

        indices->clear();
        return CompletePath();
      }
      if (segs.empty())
      {
        SPDLOG_DEBUG("Edges ab are adjacent");
      }
      else
      {
        SPDLOG_DEBUG("Edges connecting ab are {}", segs);
      }
      for (int e : segs)
      {
        complete_path.push_back(edges[e].id);
        ++current_idx;
      }
      complete_path.push_back(b->edge->id);
      ++current_idx;
      indices->push_back(current_idx);
      SPDLOG_TRACE("Insert index {}", current_idx);
    }
    else
    {
      indices->push_back(current_idx);
      SPDLOG_TRACE("Insert index {}", current_idx);
    }
  }
  return complete_path;
}

double UBODT::get_delta() const
{
  return delta;
}

std::string UBODT::get_network_hash() const
{
  return network_hash;
}

TransitionMode UBODT::get_mode() const
{
  return mode;
}

int UBODT::get_num_vertices() const
{
  return num_vertices;
}

void UBODT::insert(const Record &r)
{
  uint64_t hash = compute_hash(r.source, r.target);
  table[hash] = r;
  ++num_rows;
}

std::shared_ptr<UBODT> UBODT::read_ubodt(const std::string &filename, int progress_step)
{
  SPDLOG_INFO("Reading UBODT file from {}", filename);
  std::ifstream ifs(filename.c_str(), std::ios::binary);
  std::streampos streamEnd = ifs.seekg(0, std::ios_base::end).tellg();
  SPDLOG_INFO("Binary file size is {} bytes", streamEnd);
  ifs.seekg(0);
  boost::archive::binary_iarchive ia(ifs);

  // Read metadata header
  int version = 0;
  int mode_int = 0;
  double delta = 0;
  int num_vertices = 0;
  int stored_multiplier = 0;
  std::string network_hash = "";

  try
  {
    ia >> version >> mode_int >> delta >> num_vertices >> stored_multiplier >> network_hash;
    SPDLOG_INFO("UBODT metadata: version={}, mode={}, delta={}, num_vertices={}, multiplier={}, hash={}",
                version, mode_int, delta, num_vertices, stored_multiplier, network_hash);

    if (version != 1)
    {
      throw std::runtime_error("Unsupported UBODT file version: " + std::to_string(version));
    }
  }
  catch (const boost::archive::archive_exception &e)
  {
    // Old format file without metadata header
    SPDLOG_WARN("Unsupported UBODT file version: {}.", version);
    throw std::runtime_error("Unsupported UBODT file version: " + std::to_string(version));
  }

  // Create table with perfect hash
  SPDLOG_INFO("Number of vertices for perfect hash is {}", num_vertices);

  std::shared_ptr<UBODT> table = std::make_shared<UBODT>(num_vertices, delta, network_hash, static_cast<TransitionMode>(mode_int));

  long long num_rows = 0;
  SPDLOG_INFO("Start reading UBODT");
  while (true)
  {
    Record r;
    try
    {
      ia >> r.source;
      ia >> r.target;
      ia >> r.first_n;
      ia >> r.prev_n;
      ia >> r.next_e;
      ia >> r.cost;
      ++num_rows;
      table->insert(r);
      if (progress_step > 0 && num_rows % progress_step == 0)
      {
        SPDLOG_INFO("Read rows {}", num_rows);
      }
    }
    catch (...)
    {
      break;
    }
  }
  ifs.close();
  SPDLOG_INFO("Finish reading UBODT with rows {}", num_rows);
  SPDLOG_INFO("Perfect hash table size: {} entries", table->table.size());
  return table;
}
