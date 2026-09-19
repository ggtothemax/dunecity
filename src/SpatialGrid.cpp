#include <SpatialGrid.h>

#include <ObjectBase.h>
#include <globals.h>

#include <SDL_log.h>

#include <algorithm>
#include <stdexcept>

namespace {
constexpr const char* kLogTag = "SpatialGrid";
}

SpatialGrid::SpatialGrid(int mapWidth, int mapHeight, int cellSize)
    : cellSize(cellSize) {
    if (mapWidth <= 0 || mapHeight <= 0 || cellSize <= 0) {
        throw std::invalid_argument("Invalid dimensions for SpatialGrid");
    }

    gridWidth = (mapWidth + cellSize - 1) / cellSize;
    gridHeight = (mapHeight + cellSize - 1) / cellSize;

    try {
        grid.assign(gridHeight, std::vector<Cell>(gridWidth));
    } catch (const std::bad_alloc&) {
        throw std::runtime_error("Failed to allocate memory for spatial grid");
    }
}

bool SpatialGrid::isValidCell(int x, int y) const noexcept {
    return (x >= 0) && (x < gridWidth) && (y >= 0) && (y < gridHeight);
}

Coord SpatialGrid::toCellCoord(const Coord& tilePos) const noexcept {
    if (!tilePos.isValid()) {
        return Coord::Invalid();
    }
    return Coord(tilePos.x / cellSize, tilePos.y / cellSize);
}

SpatialGrid::Cell& SpatialGrid::getCellRef(const Coord& cell) {
    return grid[cell.y][cell.x];
}

const SpatialGrid::Cell& SpatialGrid::getCellRef(const Coord& cell) const {
    return grid[cell.y][cell.x];
}

Uint32 SpatialGrid::acquireGeneration() noexcept {
    Uint32 generation = nextGeneration++;
    if (generation == 0) {
        // Skip zero as it marks invalid handles.
        nextGeneration = 1;
        generation = nextGeneration++;
    }
    return generation;
}

void SpatialGrid::removeEntry(Cell& cell, const SpatialGridHandle& handle) noexcept {
    const auto it = std::remove_if(cell.begin(), cell.end(), [&](const SpatialGridEntry& entry) {
        return entry.key.objectId == handle.key.objectId && entry.key.generation == handle.key.generation;
    });

    if (it != cell.end()) {
        cell.erase(it, cell.end());
    } else {
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "%s: entry not found during removal (objectId=%u, gen=%u)",
                       kLogTag, handle.key.objectId, handle.key.generation);
    }
}

bool SpatialGrid::registerObject(ObjectBase& obj, const Coord& tilePos, SpatialGridHandle& handle) noexcept {
    if (!tilePos.isValid()) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s: registerObject skipped (invalid tile)", kLogTag);
        return false;
    }

    const auto cellCoord = toCellCoord(tilePos);
    if (!isCellCoordValid(cellCoord)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s: registerObject skipped (cell out of bounds %d,%d)",
                     kLogTag, cellCoord.x, cellCoord.y);
        return false;
    }

    const Uint32 objectId = obj.getObjectID();
    if (objectId == NONE_ID) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s: registerObject skipped (object has no ID)", kLogTag);
        return false;
    }

    if (handle.owner != nullptr && handle.owner != this) {
        handle.owner->unregister(handle);
    }

    if (handle.isValid()) {
        unregister(handle);
    }

    SpatialGridEntry entry;
    entry.object = &obj;
    entry.key.objectId = objectId;
    entry.key.generation = acquireGeneration();

    getCellRef(cellCoord).push_back(entry);

    handle.owner = this;
    handle.cell = cellCoord;
    handle.key = entry.key;

    return true;
}

void SpatialGrid::unregister(SpatialGridHandle& handle) noexcept {
    if (handle.owner == nullptr) {
        handle.invalidate();
        return;
    }

    if (handle.owner != this) {
        // Delegate to actual owner.
        handle.owner->unregister(handle);
        return;
    }

    if (handle.cell.isValid() && isCellCoordValid(handle.cell)) {
        removeEntry(getCellRef(handle.cell), handle);
    }

    handle.invalidate();
}

bool SpatialGrid::move(ObjectBase& obj, SpatialGridHandle& handle, const Coord& oldPos, const Coord& newPos) noexcept {
    (void)oldPos; // oldPos is currently unused but kept for future diagnostics.

    if (!newPos.isValid()) {
        unregister(handle);
        return false;
    }

    const auto newCell = toCellCoord(newPos);
    if (!isCellCoordValid(newCell)) {
        unregister(handle);
        return false;
    }

    if (handle.owner != this || !handle.isValid()) {
        return registerObject(obj, newPos, handle);
    }

    if (handle.cell == newCell) {
        // Still within the same cell; nothing to do.
        return true;
    }

    if (handle.cell.isValid() && isCellCoordValid(handle.cell)) {
        removeEntry(getCellRef(handle.cell), handle);
    }

    SpatialGridEntry entry;
    entry.object = &obj;
    entry.key = handle.key;

    getCellRef(newCell).push_back(entry);
    handle.cell = newCell;

    return true;
}

bool SpatialGrid::collectEntries(const Coord& cellCoord, std::vector<SpatialGridEntry>& out) const {
    if (!cellCoord.isValid() || !isCellCoordValid(cellCoord)) {
        return false;
    }

    const auto& cell = getCellRef(cellCoord);
    const auto begin = out.size();
    out.insert(out.end(), cell.begin(), cell.end());
    // Movement changes insertion order, while a checkpoint rebuilds the grid
    // in object-ID order. Targeting must see the same order in either case,
    // including when several equally distant targets share a cell.
    std::sort(out.begin() + begin, out.end(), [](const auto& a, const auto& b) {
        return a.key.objectId < b.key.objectId;
    });
    return true;
}

void SpatialGrid::clear() noexcept {
    for (auto& row : grid) {
        for (auto& cell : row) {
            cell.clear();
        }
    }
    nextGeneration = 1;
}

#ifdef DEBUG
void SpatialGrid::audit() const {
    for (int y = 0; y < gridHeight; ++y) {
        for (int x = 0; x < gridWidth; ++x) {
            const auto& cell = grid[y][x];
            for (const auto& entry : cell) {
                if (entry.object == nullptr) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: null entry detected at cell (%d,%d)", kLogTag, x, y);
                    continue;
                }

                if (entry.key.objectId == NONE_ID || entry.key.generation == 0) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: invalid key at cell (%d,%d)", kLogTag, x, y);
                }
            }
        }
    }
}
#endif
