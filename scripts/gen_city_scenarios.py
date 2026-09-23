#!/usr/bin/env python3
"""
Deterministic generator for the three DuneCity city-scenario maps.

Writes (into data/maps/singleplayer/):

  2P - 128x128 - Sihaya Basin.ini     town defence under a deadline   (Micropolis "Dullsville")
  3P - 128x128 - Ash Quarter.ini      recovery from ruin        (Micropolis "Detroit"/"Hamburg")
  3P - 160x96  - Coriolis Gap.ini     transport under pressure  (Micropolis "Bern")

Everything here targets mechanics the engine really implements; see
docs/city-scenarios.md for the source references.  The layout rules that
matter, all read out of the engine rather than guessed:

  * Zones are 2x2 (CLAUDE.md hard constraint, sand.cpp getStructureSize).
  * Roads are a tile flag placed with `GEN<pos>=<House>,Road` and only stick
    to rock/slab (CityConstants.h isCityBuildableTerrain).
  * A zone needs a road somewhere on its 12-tile perimeter ring and then a
    road-BFS of at most kMaxTrafficDistance = 20 tiles to a road tile that is
    4-adjacent to a structure of the complementary role
    (TrafficSimulation.cpp, CityEffects.h).  R->C, C->I, I->R.
  * Commercial growth also reads residential and industrial supply inside
    kSupplyRadius = 16 (Chebyshev).
  * Pollution falls off linearly to kPollutionRadius = 5, so residential lots
    are kept at least 6 tiles clear of industry.
  * Police coverage reaches kPoliceRadius = 23.
  * Plenty of ordinary Dune buildings already carry a city role
    (CityEffects.h getStructureCityRole): Construction Yard, Refinery, Spice
    Silo, Light/Heavy/HighTech Factory, Repair Yard and Starport are
    INDUSTRIAL; Radar/Outpost, House IX and Airport are COMMERCIAL; Barracks,
    WOR and Palace are RESIDENTIAL.  The districts below lean on those instead
    of over-zoning industrial.

Run:  python3 scripts/gen_city_scenarios.py
Then: python3 scripts/validate_city_maps.py
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Tuple

REPO = Path(__file__).resolve().parent.parent
OUT_DIR = REPO / "data" / "maps" / "singleplayer"

# ---------------------------------------------------------------------------
# Terrain characters — INIMapLoader.cpp:393-467
# ---------------------------------------------------------------------------
SAND = '-'
DUNES = '^'
SPICE = '~'
THICK = '+'
ROCK = '%'
MOUNTAIN = '@'
BLOOM = 'O'
SPECIAL = 'Q'

# ---------------------------------------------------------------------------
# Footprints — sand.cpp getStructureSize()
# ---------------------------------------------------------------------------
FOOTPRINT: Dict[str, Tuple[int, int]] = {
    "Const Yard": (2, 2),
    "Windtrap": (2, 2),
    "Refinery": (3, 2),
    "Spice Silo": (2, 2),
    "Repair Yard": (3, 2),
    "Heavy Factory": (3, 2),
    "Light Factory": (2, 2),
    "Hightech Factory": (3, 2),
    "House IX": (2, 2),
    "Palace": (3, 3),
    "Starport": (3, 3),
    "Nuclear Plant": (3, 3),
    "Stadium": (3, 3),
    "Airport": (3, 3),
    "Outpost": (2, 2),
    "Barracks": (2, 2),
    "WOR": (2, 2),
    "Police Station": (2, 2),
    "Residential Zone": (2, 2),
    "Commercial Zone": (2, 2),
    "Industrial Zone": (2, 2),
    "Rocket-Turret": (1, 1),
    "Gun-Turret": (1, 1),
    "Wall": (1, 1),
}

LOT = 3  # lot pitch: 2x2 building plus one road column/row


# ---------------------------------------------------------------------------
# Terrain canvas
# ---------------------------------------------------------------------------
class Canvas:
    def __init__(self, w: int, h: int, fill: str = SAND):
        self.w = w
        self.h = h
        self.g = [[fill] * w for _ in range(h)]

    def inside(self, x: int, y: int) -> bool:
        return 0 <= x < self.w and 0 <= y < self.h

    def set(self, x: int, y: int, ch: str) -> None:
        if self.inside(x, y):
            self.g[y][x] = ch

    def get(self, x: int, y: int) -> str:
        return self.g[y][x] if self.inside(x, y) else MOUNTAIN

    def rect(self, x: int, y: int, w: int, h: int, ch: str,
             only_if: Optional[Sequence[str]] = None) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if not self.inside(xx, yy):
                    continue
                if only_if is not None and self.g[yy][xx] not in only_if:
                    continue
                self.g[yy][xx] = ch

    def blob(self, cx: int, cy: int, r: int, ch: str, core: Optional[str] = None,
             only_if: Sequence[str] = (SAND, DUNES)) -> None:
        """Manhattan disc; `core` fills the inner half if given."""
        for yy in range(cy - r, cy + r + 1):
            for xx in range(cx - r, cx + r + 1):
                if not self.inside(xx, yy):
                    continue
                d = abs(xx - cx) + abs(yy - cy)
                if d > r or self.g[yy][xx] not in only_if:
                    continue
                self.g[yy][xx] = core if (core and d <= r // 2) else ch

    def rows(self) -> List[str]:
        return ["".join(r) for r in self.g]


# ---------------------------------------------------------------------------
# Scenario object model
# ---------------------------------------------------------------------------
@dataclass
class Placed:
    owner: str
    name: str
    x: int
    y: int
    health: int = 256


@dataclass
class Unit:
    owner: str
    name: str
    x: int
    y: int
    angle: int = 64
    mode: str = "Area Guard"
    health: int = 256


@dataclass
class Scenario:
    filename: str
    size_x: int
    size_y: int
    header: List[str]
    basic: Dict[str, object]
    houses: List[Tuple[str, Dict[str, object]]]
    canvas: Canvas
    structures: List[Placed] = field(default_factory=list)
    roads: List[Tuple[str, int, int]] = field(default_factory=list)
    units: List[Unit] = field(default_factory=list)
    reinforcements: List[str] = field(default_factory=list)
    teams: List[str] = field(default_factory=list)
    choam: Dict[str, int] = field(default_factory=dict)

    # -- placement helpers -------------------------------------------------
    def occupied(self) -> Dict[Tuple[int, int], Placed]:
        out: Dict[Tuple[int, int], Placed] = {}
        for s in self.structures:
            w, h = FOOTPRINT[s.name]
            for dy in range(h):
                for dx in range(w):
                    out[(s.x + dx, s.y + dy)] = s
        return out

    def place(self, owner: str, name: str, x: int, y: int, health: int = 256) -> Placed:
        p = Placed(owner, name, x, y, health)
        self.structures.append(p)
        return p

    def road(self, owner: str, x: int, y: int) -> None:
        self.roads.append((owner, x, y))

    def unit(self, owner: str, name: str, x: int, y: int,
             mode: str = "Area Guard", health: int = 256, angle: int = 64) -> None:
        self.units.append(Unit(owner, name, x, y, angle, mode, health))

    # -- serialisation -----------------------------------------------------
    def render(self) -> str:
        pos: Callable[[int, int], int] = lambda x, y: y * self.size_x + x
        L: List[str] = []
        L.extend(self.header)
        L.append("")
        L.append("[BASIC]")
        for k, v in self.basic.items():
            L.append(f"{k}={v}")
        L.append("")
        L.append("[MAP]")
        L.append(f"SizeX={self.size_x}")
        L.append(f"SizeY={self.size_y}")
        for y, row in enumerate(self.canvas.rows()):
            L.append(f"{y:03d}={row}")
        L.append("")
        if self.choam:
            L.append("[CHOAM]")
            for k in sorted(self.choam):
                L.append(f"{k}={self.choam[k]}")
            L.append("")
        L.append("[STRUCTURES]")
        for i, s in enumerate(self.structures, start=1):
            L.append(f"ID{i:03d}={s.owner},{s.name},{s.health},{pos(s.x, s.y)}")
        seen = set()
        for owner, x, y in self.roads:
            if (x, y) in seen:
                continue
            seen.add((x, y))
            L.append(f"GEN{pos(x, y)}={owner},Road")
        L.append("")
        L.append("[UNITS]")
        for i, u in enumerate(self.units, start=1):
            L.append(f"ID{i:03d}={u.owner},{u.name},{u.health},{pos(u.x, u.y)},{u.angle},{u.mode}")
        L.append("")
        if self.reinforcements:
            L.append("[REINFORCEMENTS]")
            for i, r in enumerate(self.reinforcements, start=1):
                L.append(f"{i}={r}")
            L.append("")
        if self.teams:
            L.append("[TEAMS]")
            for i, t in enumerate(self.teams, start=1):
                L.append(f"{i}={t}")
            L.append("")
        for name, body in self.houses:
            L.append(f"[{name}]")
            for k, v in body.items():
                L.append(f"{k}={v}")
            L.append("")
        return "\n".join(L) + "\n"


# ---------------------------------------------------------------------------
# District builder
# ---------------------------------------------------------------------------
@dataclass
class District:
    """A lot grid laid on solid rock.

    `bands[j]` is the repeating lot pattern for lot-row j:
        R residential   C commercial   I industrial
        P police station   . leave empty (plaza / build room)
    `anchors` are ordinary Dune buildings pinned to lot coordinates; they may
    be wider than a lot and simply eat the road tile beside them — the grid
    stays connected through the perpendicular streets.
    """
    owner: str
    ox: int
    oy: int
    cols: int
    rows: int
    bands: List[str]
    anchors: List[Tuple[str, int, int]] = field(default_factory=list)
    anchor_health: int = 256
    zone_health: int = 256
    skip_lots: Sequence[Tuple[int, int]] = ()

    def lot_tile(self, i: int, j: int) -> Tuple[int, int]:
        return self.ox + LOT * i, self.oy + LOT * j

    def extent(self) -> Tuple[int, int, int, int]:
        """Rock rectangle the district needs, including its ring road."""
        return (self.ox - 2, self.oy - 2, LOT * self.cols + 3, LOT * self.rows + 3)


def build_district(sc: Scenario, d: District) -> None:
    occ = sc.occupied()
    blocked = set(occ.keys())

    def free(x: int, y: int, w: int, h: int) -> bool:
        for dy in range(h):
            for dx in range(w):
                if (x + dx, y + dy) in blocked:
                    return False
                if sc.canvas.get(x + dx, y + dy) != ROCK:
                    return False
        return True

    def take(x: int, y: int, w: int, h: int) -> None:
        for dy in range(h):
            for dx in range(w):
                blocked.add((x + dx, y + dy))

    # 1) Dune anchors first — they are the industrial/commercial/residential
    #    backbone the zones will trade with.
    for name, i, j in d.anchors:
        w, h = FOOTPRINT[name]
        x, y = d.lot_tile(i, j)
        if not free(x, y, w, h):
            raise ValueError(f"anchor {name} at lot ({i},{j}) -> ({x},{y}) does not fit")
        sc.place(d.owner, name, x, y, d.anchor_health)
        take(x, y, w, h)

    # 2) Zone fill from the band patterns.
    skip = set(d.skip_lots)
    name_for = {
        'R': "Residential Zone",
        'C': "Commercial Zone",
        'I': "Industrial Zone",
        'P': "Police Station",
    }
    for j in range(d.rows):
        band = d.bands[j % len(d.bands)]
        for i in range(d.cols):
            if (i, j) in skip:
                continue
            code = band[i % len(band)]
            if code == '.':
                continue
            x, y = d.lot_tile(i, j)
            if not free(x, y, 2, 2):
                continue
            sc.place(d.owner, name_for[code], x, y, d.zone_health)
            take(x, y, 2, 2)

    # 3) Street grid: every third column and row, plus a ring road just
    #    outside the lot grid so the perimeter lots keep frontage.
    x0, y0 = d.ox - 1, d.oy - 1
    x1, y1 = d.ox + LOT * d.cols - 1, d.oy + LOT * d.rows - 1
    road_tiles = set()
    for j in range(d.rows):
        ry = d.oy + LOT * j + 2
        for x in range(x0, x1 + 1):
            road_tiles.add((x, ry))
    for i in range(d.cols):
        rx = d.ox + LOT * i + 2
        for y in range(y0, y1 + 1):
            road_tiles.add((rx, y))
    for x in range(x0, x1 + 1):
        road_tiles.add((x, y0))
        road_tiles.add((x, y1))
    for y in range(y0, y1 + 1):
        road_tiles.add((x0, y))
        road_tiles.add((x1, y))

    for (x, y) in sorted(road_tiles):
        if (x, y) in blocked:
            continue
        if sc.canvas.get(x, y) != ROCK:
            continue
        sc.road(d.owner, x, y)


def link_road(sc: Scenario, owner: str, a: Tuple[int, int], b: Tuple[int, int]) -> None:
    """L-shaped road run between two tiles (horizontal leg first)."""
    occ = set(sc.occupied().keys())
    ax, ay = a
    bx, by = b
    for x in range(min(ax, bx), max(ax, bx) + 1):
        if (x, ay) not in occ and sc.canvas.get(x, ay) == ROCK:
            sc.road(owner, x, ay)
    for y in range(min(ay, by), max(ay, by) + 1):
        if (bx, y) not in occ and sc.canvas.get(bx, y) == ROCK:
            sc.road(owner, bx, y)


def perimeter_defence(sc: Scenario, owner: str, rect: Tuple[int, int, int, int],
                      spacing: int = 9, rocket_every: int = 3) -> None:
    """Drop turrets on free rock around a district rectangle."""
    x, y, w, h = rect
    ring: List[Tuple[int, int]] = []
    for i in range(0, w):
        ring.append((x + i, y))
    for i in range(0, h):
        ring.append((x + w - 1, y + i))
    for i in range(w - 1, -1, -1):
        ring.append((x + i, y + h - 1))
    for i in range(h - 1, -1, -1):
        ring.append((x, y + i))
    occ = set(sc.occupied().keys())
    roads = {(rx, ry) for _o, rx, ry in sc.roads}
    n = 0
    for idx, (tx, ty) in enumerate(ring):
        if idx % spacing:
            continue
        if (tx, ty) in occ or (tx, ty) in roads:
            continue
        if sc.canvas.get(tx, ty) != ROCK:
            continue
        sc.place(owner, "Rocket-Turret" if n % rocket_every == 0 else "Gun-Turret", tx, ty)
        occ.add((tx, ty))
        n += 1


def free_spot(sc: Scenario, x: int, y: int, terrain: Sequence[str] = (ROCK, SAND, DUNES),
              used: Optional[set] = None) -> Tuple[int, int]:
    """Nearest tile to (x,y) that has no structure and acceptable terrain."""
    occ = set(sc.occupied().keys())
    occ |= {(u.x, u.y) for u in sc.units}
    if used:
        occ |= used
    for r in range(0, 24):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if r and max(abs(dx), abs(dy)) != r:
                    continue
                tx, ty = x + dx, y + dy
                if not sc.canvas.inside(tx, ty):
                    continue
                if (tx, ty) in occ:
                    continue
                if sc.canvas.get(tx, ty) in terrain:
                    return tx, ty
    return x, y


def garrison(sc: Scenario, owner: str, cx: int, cy: int,
             loadout: Sequence[Tuple[str, str]]) -> None:
    used: set = set()
    for i, (name, mode) in enumerate(loadout):
        gx = cx + (i % 5) * 2 - 4
        gy = cy + (i // 5) * 2 - 2
        ux, uy = free_spot(sc, gx, gy, used=used)
        used.add((ux, uy))
        sc.unit(owner, name, ux, uy, mode)


DEFAULT_CHOAM = {
    "Carryall": 2, "Harvester": 4, "Launcher": 5, "MCV": 2,
    "'Thopter": 4, "Quad": 5, "Siege Tank": 5, "Tank": 6, "Trike": 5,
}


# ===========================================================================
# Scenario 1 — Sihaya Basin  (Micropolis "Dullsville", notice_50)
# ===========================================================================
def scenario_sihaya_basin() -> Scenario:
    W = H = 128
    c = Canvas(W, H)

    # A long rock shelf in the west: the company town.  Open dune sea east,
    # spice belts north and south-east, a Harkonnen shelf on the east rim.
    c.rect(8, 22, 54, 84, ROCK)
    # Broken rim so the basin is defensible but not sealed.
    for y in range(18, 112, 1):
        if 26 <= y <= 40 or 58 <= y <= 70 or 88 <= y <= 100:
            continue
        c.set(63, y, MOUNTAIN)
        c.set(64, y, MOUNTAIN)
    c.rect(8, 18, 54, 4, MOUNTAIN)
    c.rect(8, 106, 54, 4, MOUNTAIN)

    # Harkonnen shelf.
    c.rect(96, 40, 24, 44, ROCK)
    c.rect(92, 36, 4, 52, MOUNTAIN)

    # Expansion rock the player has to walk out to.
    for (rx, ry, rw, rh) in [(70, 30, 10, 8), (74, 62, 12, 9), (70, 96, 10, 8)]:
        c.rect(rx, ry, rw, rh, ROCK)

    # Spice.
    for (sx, sy, r, thick) in [
        (76, 20, 9, True), (84, 100, 10, True), (70, 52, 6, False),
        (68, 82, 6, False), (110, 100, 8, True), (110, 22, 7, False),
        (30, 114, 8, True), (30, 12, 7, False),
    ]:
        c.blob(sx, sy, r, SPICE, THICK if thick else None)
    c.set(78, 46, BLOOM)
    c.set(78, 88, BLOOM)
    for (dx, dy) in [(88, 60), (100, 108), (66, 12)]:
        c.blob(dx, dy, 5, DUNES)

    sc = Scenario(
        filename="2P - 128x128 - Sihaya Basin.ini",
        size_x=W, size_y=H,
        header=[
            "; Sihaya Basin — 128x128, 2 houses, DuneCity city-sim scenario.",
            ";",
            "; \"Nothing has changed here in a hundred standard years and the",
            ";  residents are bored.\"  A Micropolis-Dullsville premise moved to",
            ";  Arrakis: you inherit a stagnant, fully-built company town on the",
            ";  Sihaya shelf and have to secure its economy and defeat the raiders before the",
            ";  CHOAM audit lands — while a Harkonnen shelf across the dune sea",
            ";  decides the town is easier to take than to out-grow.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Opponent: Harkonnen.",
            "; Win     : destroy the Harkonnen base before the 60-minute audit.",
            "; Lose    : your buildings gone, or the clock expires.",
            "; Generated by scripts/gen_city_scenarios.py — do not hand-edit.",
        ],
        basic={
            "Version": 2,
            "License": "CC-BY-SA",
            "Author": "DuneCity",
            "TechLevel": 8,
            # 1 AI_NO_BUILDINGS | 2 HUMAN_HAS_BUILDINGS | 8 TIMEOUT
            "WinFlags": 11,
            "LoseFlags": 1,
            "LosePicture": "LOSTBILD.WSA",
            "WinPicture": "WIN1.WSA",
            "BriefPicture": "ATTACK.WSA",
            "TimeOut": 60,
        },
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 9000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 7000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # --- The town -----------------------------------------------------------
    # Lot grid origin (12, 27); 15 lot columns x 24 lot rows fits the shelf.
    #
    # Band schedule, top to bottom (each band is one lot row = 3 tiles):
    #   0-2  north works: Dune industry anchors + a little industrial zoning
    #   3-4  commercial spine (pollution buffer AND the R->C destination)
    #   5-12 residential core with a commercial street every third row
    #   13-14 second commercial spine
    #   15-17 south works: repair/refit yard + industrial zoning
    #   18-23 civic/residential expansion with police cover
    bands = [
        "IIIII.III",        # 0
        "I.III.III",        # 1
        "III.IIIII",        # 2
        "CCCCCCCCC",        # 3
        "CCPCCCCPC",        # 4
        "RRRRRRRRR",        # 5
        "RRRRRRRRR",        # 6
        "RRCRRCRRC",        # 7
        "RRRRRRRRR",        # 8
        "RRRRRRRRR",        # 9
        "RRCRRCRRC",        # 10
        "RRRRRRRRR",        # 11
        "RPRRRRRPR",        # 12
        "CCCCCCCCC",        # 13
        "CCCCCCCCC",        # 14
        "III.IIIII",        # 15
        "IIIII.III",        # 16
        "I.III.III",        # 17
        "CCCCCCCCC",        # 18
        "RRRRRRRRR",        # 19
        "RRCRRCRRC",        # 20
        "RRRRRRRRR",        # 21
        "RPRRRRRPR",        # 22
        "RRRRRRRRR",        # 23
    ]
    anchors = [
        # North works — every one of these counts as INDUSTRIAL in the sim.
        ("Const Yard", 0, 0), ("Refinery", 2, 0), ("Heavy Factory", 5, 0),
        ("Windtrap", 7, 0), ("Windtrap", 8, 0),
        ("Nuclear Plant", 0, 1), ("Spice Silo", 3, 1), ("Spice Silo", 4, 1),
        ("Light Factory", 6, 1), ("Windtrap", 8, 1),
        ("Repair Yard", 0, 2), ("Hightech Factory", 3, 2), ("Windtrap", 6, 2),
        # Commercial spine gets the commercial-role Dune buildings.
        ("Outpost", 4, 3), ("House IX", 7, 4),
        # Residential-role Dune buildings inside the housing core.
        ("Barracks", 0, 8), ("WOR", 8, 11),
        # South works.
        ("Nuclear Plant", 0, 15), ("Refinery", 3, 15), ("Spice Silo", 6, 15),
        ("Windtrap", 8, 16), ("Light Factory", 5, 16),
        ("Outpost", 2, 18), ("Airport", 6, 18),
    ]
    district = District(
        owner="Atreides", ox=12, oy=27, cols=9, rows=24,
        bands=bands, anchors=anchors,
    )
    build_district(sc, district)
    perimeter_defence(sc, "Atreides", (10, 25, 33, 76), spacing=11)

    # Harvesters, a modest garrison and the CHOAM-era air pair.
    garrison(sc, "Atreides", 22, 34, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Quad", "Area Guard"), ("Trike", "Area Guard"),
        ("Launcher", "Area Guard"), ("Carryall", "Area Guard"),
    ])

    # --- Harkonnen shelf ----------------------------------------------------
    hk = District(
        owner="Harkonnen", ox=100, oy=44, cols=6, rows=11,
        bands=["IIIIII", "CCCCCC", "RRCCRR", "RRRRRR", "RRCCRR",
               "RPRRPR", "CCCCCC", "IIIIII", "CCCCCC", "RRCCRR", "RPRRPR"],
        anchors=[
            ("Const Yard", 0, 0), ("Refinery", 2, 0), ("Heavy Factory", 3, 0),
            ("Nuclear Plant", 0, 1), ("Windtrap", 4, 1),
            ("Repair Yard", 0, 7), ("Outpost", 4, 6), ("Barracks", 5, 2),
        ],
    )
    build_district(sc, hk)
    perimeter_defence(sc, "Harkonnen", (97, 41, 22, 40), spacing=8)
    garrison(sc, "Harkonnen", 108, 52, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"), ("Siege Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
        ("Carryall", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Guard,Foot,2,4",
        "Harkonnen,Kamikaze,Tracked,3,6",
        "Harkonnen,Normal,Tracked,4,8",
    ]
    # Pressure builds: probing trikes early, armour later, repeating.
    sc.reinforcements = [
        "Harkonnen,Trike,Enemybase,9",
        "Harkonnen,Quad,Enemybase,9",
        "Harkonnen,Tank,Enemybase,20",
        "Harkonnen,Tank,Enemybase,20",
        "Harkonnen,Launcher,Enemybase,32",
        "Harkonnen,Siege Tank,Enemybase,32",
        "Harkonnen,Tank,Enemybase,44,+",
        "Harkonnen,Launcher,Enemybase,44,+",
        # CHOAM keeps the town supplied while it grows.
        "Atreides,Harvester,Homebase,14",
        "Atreides,Harvester,Homebase,30",
        "Atreides,Quad,Homebase,22",
        "Atreides,Tank,Homebase,36,+",
    ]
    return sc


# ===========================================================================
# Scenario 2 — Ash Quarter  (Micropolis "Detroit" + "Hamburg", notice_55/52)
# ===========================================================================
def scenario_ash_quarter() -> Scenario:
    W = H = 128
    c = Canvas(W, H)

    # A big southern city plateau that has been shelled: craters (sand) punched
    # through the rock, a ruined northern industrial annex, raider camps on the
    # north rim.
    c.rect(14, 54, 84, 62, ROCK)
    c.rect(20, 24, 40, 20, ROCK)          # burnt-out north annex
    c.rect(78, 20, 30, 24, ROCK)          # scavenger shelf
    c.rect(64, 18, 8, 30, MOUNTAIN)       # ridge between annex and scavengers
    c.rect(10, 50, 92, 3, MOUNTAIN)
    c.rect(10, 50, 3, 3, ROCK)
    c.rect(44, 50, 14, 3, ROCK)           # northern gate into the city
    c.rect(86, 50, 10, 3, ROCK)

    # Bomb craters inside the city plateau — sand pockets, buildable for zones
    # (isCityZoneTerrain allows sand) but not for roads, so they read as scars.
    # Kept clear of the works rows (y 57-65 and y 93-101) so the surviving
    # Dune buildings still stand on rock.
    for (cx, cy, r) in [(30, 72, 3), (52, 86, 4), (74, 70, 3), (44, 108, 3),
                        (86, 105, 4), (24, 80, 3), (66, 112, 3)]:
        c.blob(cx, cy, r, SAND, only_if=(ROCK,))

    for (sx, sy, r, thick) in [
        (110, 70, 9, True), (112, 112, 8, True), (8, 20, 9, True),
        (40, 8, 8, False), (104, 8, 7, False), (6, 100, 7, False),
        (70, 6, 6, False),
    ]:
        c.blob(sx, sy, r, SPICE, THICK if thick else None)
    c.set(102, 46, BLOOM)
    c.set(12, 46, BLOOM)
    for (dx, dy) in [(90, 120), (18, 8), (120, 40)]:
        c.blob(dx, dy, 5, DUNES)

    sc = Scenario(
        filename="3P - 128x128 - Ash Quarter.ini",
        size_x=W, size_y=H,
        header=[
            "; Ash Quarter — 128x128, 3 houses, DuneCity city-sim scenario.",
            ";",
            "; Micropolis crossed two recovery briefs here: Detroit 1972 (land",
            "; values collapse, industry dies, crime becomes chronic) and",
            "; Hamburg 1944 (rebuild a shelled city).  The Ash Quarter is an",
            "; Atreides refinery town that lost its northern works to a raid: the",
            "; plateau is cratered, half the buildings are on fire-damaged",
            "; hitpoints, the police precincts are gone and the surviving",
            "; industry has no road to anywhere.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Harkonnen raiders (north-west), Mercenary scavengers",
            ";           squatting the north-east shelf.",
            "; Win     : rebuild your foothold and clear both hostile houses.",
            "; Lose    : your buildings gone.",
            "; Generated by scripts/gen_city_scenarios.py — do not hand-edit.",
        ],
        basic={
            "Version": 2,
            "License": "CC-BY-SA",
            "Author": "DuneCity",
            "TechLevel": 8,
            # 1 AI_NO_BUILDINGS | 2 HUMAN_HAS_BUILDINGS
            "WinFlags": 3,
            "LoseFlags": 1,
            "LosePicture": "LOSTBILD.WSA",
            "WinPicture": "WIN1.WSA",
            "BriefPicture": "MACHINE.WSA",
            "TimeOut": 0,
        },
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 6500, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 6000, "Quota": 0}),
            ("Mercenary", {"Brain": "Team2", "Credits": 4000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # --- The surviving Ash Quarter -----------------------------------------
    # Deliberately lopsided: lots of industrial zoning, thin commercial, only
    # two police stations left standing.  That is the Detroit problem — crime
    # and pollution eat land value until the player re-zones and re-polices.
    bands = [
        "IIIIIIIIIIIIIIIIII",   # 0  surviving works
        "II.IIIIII.IIIIIIII",   # 1
        "IIIIII.IIIIIII.III",   # 2
        "CCCCCCCCCCCCCCCCCC",   # 3  thin commercial buffer
        "RRRRRRRRRRRRRRRRRR",   # 4
        "RRRRRRRRRRRRRRRRRR",   # 5
        "RRCRRCRRCRRCRRCRRC",   # 6
        "RRRRRRRRRRRRRRRRRR",   # 7
        "R.RRR.RRR.RRR.RRR.",   # 8  cratered row — gaps are build room
        "RRCRRCRRCRRCRRCRRC",   # 9
        "RRRRRRRRRRRRRRRRRR",   # 10
        "CCCCCCCCCCCCCCCCCC",   # 11
        "IIIIII.IIIIIII.III",   # 12 south works
        "II.IIIIII.IIIIIIII",   # 13
        "CCCCCCCCCCCCCCCCCC",   # 14
        "RRRRRRRRRRRRRRRRRR",   # 15
        "RRCRRCRRCRRCRRCRRC",   # 16
        "RRRRRRRRRRRRRRRRRR",   # 17
        "RPRRRRRRPRRRRRRPRR",   # 18 the two surviving precincts
    ]
    anchors = [
        ("Const Yard", 0, 0), ("Refinery", 2, 0), ("Heavy Factory", 5, 0),
        ("Windtrap", 8, 0), ("Windtrap", 9, 0), ("Light Factory", 12, 0),
        ("Nuclear Plant", 0, 1), ("Spice Silo", 3, 1), ("Repair Yard", 6, 1),
        ("Windtrap", 10, 1), ("Hightech Factory", 13, 1),
        ("Outpost", 5, 3), ("House IX", 12, 3),
        ("Barracks", 0, 7), ("WOR", 16, 9),
        ("Refinery", 2, 12), ("Spice Silo", 6, 12), ("Windtrap", 9, 12),
        ("Nuclear Plant", 12, 12),
        ("Outpost", 3, 14), ("Airport", 10, 14),
    ]
    city = District(
        owner="Atreides", ox=17, oy=57, cols=18, rows=19,
        bands=bands, anchors=anchors,
        anchor_health=190,   # fire damage — recovery, not a fresh base
        zone_health=150,
    )
    build_district(sc, city)
    perimeter_defence(sc, "Atreides", (15, 55, 80, 59), spacing=13)

    garrison(sc, "Atreides", 30, 64, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Quad", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
        ("Carryall", "Area Guard"),
    ])

    # --- Ruined northern annex (neutral rubble the player can re-take) ------
    # Left as free rock plus a couple of intact Atreides outbuildings on low HP.
    for (nm, x, y, hp) in [("Windtrap", 24, 28, 80), ("Spice Silo", 28, 28, 70),
                           ("Light Factory", 32, 28, 90), ("Outpost", 37, 28, 60)]:
        sc.place("Atreides", nm, x, y, hp)
    for x in range(22, 44):
        if sc.canvas.get(x, 30) == ROCK:
            sc.road("Atreides", x, 30)

    # --- Harkonnen raider camp (north-west) --------------------------------
    hk = District(
        owner="Harkonnen", ox=22, oy=34, cols=5, rows=3,
        bands=["IIIII", "CCCCC", "RRCCR"],
        anchors=[("Const Yard", 0, 0), ("Heavy Factory", 2, 0), ("Barracks", 4, 2)],
    )
    build_district(sc, hk)
    for (nm, x, y) in [("Windtrap", 22, 26), ("Refinery", 33, 24),
                       ("Rocket-Turret", 44, 34), ("Gun-Turret", 44, 40),
                       ("Rocket-Turret", 20, 32)]:
        sc.place("Harkonnen", nm, x, y)
    garrison(sc, "Harkonnen", 30, 40, [
        ("Harvester", "Harvest"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
    ])

    # --- Mercenary scavengers (north-east shelf) ---------------------------
    mc = District(
        owner="Mercenary", ox=82, oy=24, cols=6, rows=4,
        bands=["IIIIII", "CCCCCC", "RRRRRR", "RPRRPR"],
        anchors=[("Const Yard", 0, 0), ("Refinery", 2, 0), ("Windtrap", 5, 0),
                 ("Outpost", 4, 1)],
    )
    build_district(sc, mc)
    perimeter_defence(sc, "Mercenary", (80, 22, 26, 20), spacing=7)
    garrison(sc, "Mercenary", 94, 34, [
        ("Harvester", "Harvest"), ("Quad", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Guard,Foot,2,4",
        "Harkonnen,Normal,Tracked,4,8",
        "Harkonnen,Kamikaze,Tracked,2,5",
        "Mercenary,Guard,Foot,2,4",
        "Mercenary,Normal,Wheeled,3,6",
    ]
    sc.reinforcements = [
        # Raiders probe the gap and the northern gate on a rising schedule.
        "Harkonnen,Trike,Enemybase,7",
        "Harkonnen,Quad,Enemybase,7",
        "Harkonnen,Tank,Enemybase,18",
        "Harkonnen,Launcher,Enemybase,18",
        "Harkonnen,Tank,Enemybase,30,+",
        "Mercenary,Raider Trike,Enemybase,13",
        "Mercenary,Quad,Enemybase,26,+",
        # Relief convoys for the rebuild.
        "Atreides,Harvester,Homebase,6",
        "Atreides,Harvester,Homebase,16",
        "Atreides,Quad,Homebase,11",
        "Atreides,Launcher,Homebase,24,+",
    ]
    return sc


# ===========================================================================
# Scenario 3 — Coriolis Gap  (Micropolis "Bern", notice_53)
# ===========================================================================
def scenario_coriolis_gap() -> Scenario:
    W, H = 160, 96
    c = Canvas(W, H)

    # Three shelves in a row, separated by mountain walls with a single narrow
    # rock pass each.  The passes are deliberately left unpaved.
    c.rect(6, 22, 40, 54, ROCK)      # west shelf  — housing
    c.rect(58, 18, 40, 62, ROCK)     # centre shelf — works + commerce
    c.rect(110, 26, 38, 46, ROCK)    # east shelf  — the Harkonnen quarter

    c.rect(46, 14, 12, 70, MOUNTAIN)
    c.rect(98, 14, 12, 70, MOUNTAIN)
    # The two gaps: 6 tiles of bare rock the player has to pave.
    c.rect(46, 46, 12, 6, ROCK)
    c.rect(98, 44, 12, 6, ROCK)
    # A second, longer southern detour keeps the map from being a single choke.
    c.rect(46, 78, 12, 5, ROCK)
    c.rect(6, 76, 92, 7, ROCK)

    for (sx, sy, r, thick) in [
        (22, 10, 8, True), (80, 8, 9, True), (132, 12, 8, True),
        (150, 60, 8, False), (14, 88, 8, False), (76, 90, 7, False),
        (120, 88, 7, True), (4, 50, 5, False),
    ]:
        c.blob(sx, sy, r, SPICE, THICK if thick else None)
    c.set(52, 12, BLOOM)
    c.set(104, 86, BLOOM)
    for (dx, dy) in [(100, 90), (56, 6), (148, 88)]:
        c.blob(dx, dy, 5, DUNES)

    sc = Scenario(
        filename="3P - 160x96 - Coriolis Gap.ini",
        size_x=W, size_y=H,
        header=[
            "; Coriolis Gap — 160x96, 3 houses, DuneCity city-sim scenario.",
            ";",
            "; Micropolis Bern 1965: \"the roads are more congested every day and",
            "; the residents are upset... this would require major rezoning\".",
            "; Here the problem is the opposite extreme — there is no road at",
            "; all through the two Coriolis passes.  Your housing sits on the",
            "; west shelf, your main works on the centre shelf. Each neighbourhood",
            "; has local shops and industry within the 20-tile traffic limit.",
            "; Freight depots (Spice Silos) supply the western shops without",
            "; polluting the housing. The blocked passes divide your military",
            "; forces and harvester routes: clear the garrison, connect the",
            "; corridor and advance on the Harkonnen east quarter.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Harkonnen east quarter, Sardaukar garrison on the pass.",
            "; Win     : clear the Sardaukar blockade and the Harkonnen city.",
            "; Generated by scripts/gen_city_scenarios.py — do not hand-edit.",
        ],
        basic={
            "Version": 2,
            "License": "CC-BY-SA",
            "Author": "DuneCity",
            "TechLevel": 8,
            "WinFlags": 3,
            "LoseFlags": 1,
            "LosePicture": "LOSTBILD.WSA",
            "WinPicture": "WIN1.WSA",
            "BriefPicture": "BLOODY.WSA",
            "TimeOut": 0,
        },
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 8000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 7000, "Quota": 0}),
            ("Sardaukar", {"Brain": "Team2", "Credits": 3000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # --- West shelf: housing, shops and local nonpolluting freight depots ----
    west = District(
        owner="Atreides", ox=10, oy=26, cols=11, rows=15,
        bands=[
            "CCCCCCCCCCC",
            "RRRRRRRRRRR",
            "RRCRRCRRCRR",
            "RRRRRRRRRRR",
            "RPRRRRRRRPR",
            "RRRRRRRRRRR",
            "RRCRRCRRCRR",
            "RRRRRRRRRRR",
            "CCCCCCCCCCC",
            "RRRRRRRRRRR",
            "RRCRRCRRCRR",
            "RRRRRRRRRRR",
            "RPRRRRRRRPR",
            "RRRRRRRRRRR",
            "CCCCCCCCCCC",
        ],
        anchors=[
            ("Windtrap", 0, 0), ("Windtrap", 1, 0), ("Outpost", 3, 0),
            ("Nuclear Plant", 8, 0),
            ("Barracks", 0, 7), ("WOR", 10, 7), ("House IX", 5, 8),
            ("Windtrap", 0, 14), ("Airport", 7, 13),
            ("Spice Silo", 5, 0), ("Spice Silo", 2, 8),
            ("Spice Silo", 8, 8), ("Spice Silo", 5, 14),
        ],
    )
    build_district(sc, west)
    perimeter_defence(sc, "Atreides", (8, 24, 38, 50), spacing=10)

    # --- Centre shelf: the works, plus commerce that wants customers --------
    centre = District(
        owner="Atreides", ox=62, oy=22, cols=11, rows=18,
        bands=[
            "IIIIIIIIIII",
            "II.IIIII.II",
            "IIIIIIIIIII",
            "CCCCCCCCCCC",
            "CCPCCCCCPCC",
            "IIIIIIIIIII",
            "II.IIIII.II",
            "CCCCCCCCCCC",
            "RRRRRRRRRRR",
            "RRCRRCRRCRR",
            "RRRRRRRRRRR",
            "RPRRRRRRRPR",
            "CCCCCCCCCCC",
            "IIIIIIIIIII",
            "II.IIIII.II",
            "CCCCCCCCCCC",
            "RRRRRRRRRRR",
            "RRCRRCRRCRR",
        ],
        anchors=[
            ("Const Yard", 0, 0), ("Refinery", 2, 0), ("Heavy Factory", 5, 0),
            ("Windtrap", 8, 0), ("Windtrap", 9, 0),
            ("Nuclear Plant", 0, 1), ("Repair Yard", 3, 1), ("Light Factory", 7, 1),
            ("Hightech Factory", 0, 2), ("Spice Silo", 4, 2), ("Spice Silo", 5, 2),
            ("Outpost", 3, 3), ("House IX", 8, 4),
            ("Barracks", 0, 3), ("WOR", 7, 3),
            ("Nuclear Plant", 0, 13), ("Refinery", 4, 13), ("Spice Silo", 8, 13),
            ("Airport", 5, 15), ("Barracks", 0, 16),
        ],
    )
    build_district(sc, centre)
    perimeter_defence(sc, "Atreides", (60, 20, 38, 58), spacing=12)

    garrison(sc, "Atreides", 74, 30, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
        ("Carryall", "Area Guard"), ("'Thopter", "Area Guard"),
    ])
    garrison(sc, "Atreides", 22, 36, [
        ("Quad", "Area Guard"), ("Quad", "Area Guard"), ("Trike", "Area Guard"),
        ("Launcher", "Area Guard"),
    ])
    # Deliberately NO roads through the gaps — that is the scenario.
    # A short stub on each side shows the player where the link belongs.
    link_road(sc, "Atreides", (44, 48), (46, 48))
    link_road(sc, "Atreides", (57, 48), (59, 48))

    # --- Sardaukar blocking garrison sitting on the western pass ----------
    for (nm, x, y) in [("Gun-Turret", 49, 46), ("Rocket-Turret", 49, 51),
                       ("Gun-Turret", 55, 46), ("Rocket-Turret", 55, 51),
                       ("Windtrap", 51, 47), ("Barracks", 47, 47)]:
        sc.place("Sardaukar", nm, x, y)
    garrison(sc, "Sardaukar", 52, 48, [
        ("Trooper", "Area Guard"), ("Trooper", "Area Guard"),
        ("Quad", "Area Guard"), ("Tank", "Area Guard"),
    ])

    # --- Harkonnen east quarter -------------------------------------------
    east = District(
        owner="Harkonnen", ox=114, oy=30, cols=10, rows=13,
        bands=[
            "IIIIIIIIII",
            "II.IIII.II",
            "CCCCCCCCCC",
            "RRRRRRRRRR",
            "RRCRRCRRCR",
            "RRRRRRRRRR",
            "RPRRRRRRPR",
            "CCCCCCCCCC",
            "IIIIIIIIII",
            "CCCCCCCCCC",
            "RRRRRRRRRR",
            "RRCRRCRRCR",
            "RPRRRRRRPR",
        ],
        anchors=[
            ("Const Yard", 0, 0), ("Refinery", 2, 0), ("Heavy Factory", 5, 0),
            ("Windtrap", 8, 0), ("Nuclear Plant", 0, 1), ("Repair Yard", 4, 1),
            ("Outpost", 3, 2), ("Barracks", 0, 10), ("Palace", 7, 10),
        ],
    )
    build_district(sc, east)
    perimeter_defence(sc, "Harkonnen", (112, 28, 36, 42), spacing=8)
    garrison(sc, "Harkonnen", 128, 40, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"), ("Siege Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
        ("Devastator", "Area Guard"), ("Carryall", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Guard,Foot,2,4",
        "Harkonnen,Normal,Tracked,4,8",
        "Harkonnen,Kamikaze,Tracked,3,6",
        "Sardaukar,Guard,Foot,2,4",
        "Sardaukar,Guard,Tracked,2,4",
    ]
    sc.reinforcements = [
        # The Harkonnen keep pushing at the passes — the roads you lay are the
        # corridor they threaten through ordinary combat.
        "Harkonnen,Trike,Enemybase,8",
        "Harkonnen,Quad,Enemybase,8",
        "Harkonnen,Tank,Enemybase,19",
        "Harkonnen,Launcher,Enemybase,19",
        "Harkonnen,Siege Tank,Enemybase,31",
        "Harkonnen,Tank,Enemybase,31",
        "Harkonnen,Tank,Enemybase,42,+",
        "Harkonnen,Launcher,Enemybase,42,+",
        "Sardaukar,Troopers,Homebase,15,+",
        # Atreides convoys arrive on the west shelf and have to drive the gap.
        "Atreides,Harvester,Homebase,12",
        "Atreides,Quad,Homebase,18",
        "Atreides,Tank,Homebase,28,+",
    ]
    return sc


# ---------------------------------------------------------------------------
def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for factory in (scenario_sihaya_basin, scenario_ash_quarter, scenario_coriolis_gap):
        sc = factory()
        text = sc.render()
        path = OUT_DIR / sc.filename
        path.write_text(text)
        roads = len({(x, y) for _o, x, y in sc.roads})
        print(f"{sc.filename}: {len(sc.structures)} structures, "
              f"{roads} road tiles, {len(sc.units)} units, "
              f"{len(sc.reinforcements)} reinforcements, {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
