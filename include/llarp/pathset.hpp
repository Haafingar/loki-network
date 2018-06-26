#ifndef LLARP_PATHSET_HPP
#define LLARP_PATHSET_HPP

#include <llarp/path_types.hpp>
#include <map>
#include <tuple>

namespace llarp
{
  namespace path
  {
    enum PathStatus
    {
      ePathBuilding,
      ePathEstablished,
      ePathTimeout,
      ePathExpired
    };
    // forward declare
    struct Path;

    /// a set of paths owned by an entity
    struct PathSet
    {
      /// construct
      /// @params numPaths the number of paths to maintain
      PathSet(size_t numPaths);

      void
      RemovePath(Path* path);

      void
      HandlePathBuilt(Path* path);

      void
      AddPath(Path* path);

      Path*
      GetByUpstream(const RouterID& remote, const PathID_t& rxid);

      void
      ExpirePaths(llarp_time_t now);

      size_t
      NumInStatus(PathStatus st) const;

      /// return true if we should build another path
      bool
      ShouldBuildMore() const;

     private:
      typedef std::map< PathID_t, Path* > PathMap_t;
      // (tx,rx)
      typedef std::tuple< PathMap_t, PathMap_t > PathContainer_t;

      size_t m_NumPaths;
      PathContainer_t m_Paths;
    };

  }  // namespace path
}  // namespace llarp

#endif