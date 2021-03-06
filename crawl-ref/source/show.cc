/**
 * @file
 * @brief Updates the screen via map_knowledge.
**/

#include "AppHdr.h"

#include "show.h"

#include "areas.h"
#include "cloud.h"
#include "coord.h"
#include "coordit.h"
#include "dgnevent.h"
#include "dgn-overview.h"
#include "directn.h"
#include "dungeon.h"
#include "env.h"
#include "exclude.h"
#include "fprop.h"
#include "itemprop.h"
#include "libutil.h"
#include "mon-place.h"
#include "mon-util.h"
#include "monster.h"
#include "options.h"
#include "random.h"
#include "stash.h"
#include "state.h"
#include "areas.h"
#include "terrain.h"
#ifdef USE_TILE
 #include "tileview.h"
#endif
#include "tiledef-main.h"
#include "traps.h"
#include "travel.h"
#include "viewgeom.h"
#include "viewmap.h"

show_type::show_type()
    : cls(SH_NOTHING),
      feat(DNGN_UNSEEN),
      item(SHOW_ITEM_NONE),
      mons(MONS_NO_MONSTER),
      colour(0)
{
}

show_type::show_type(monster_type montype)
    : cls(SH_MONSTER),
      feat(DNGN_UNSEEN),
      item(SHOW_ITEM_NONE),
      mons(montype),
      colour(0)
{
}

show_type::show_type(dungeon_feature_type f)
    : cls(SH_FEATURE),
      feat(f),
      item(SHOW_ITEM_NONE),
      mons(MONS_NO_MONSTER),
      colour(0)
{
}

static show_item_type _item_to_show_code(const item_def &item);

show_type::show_type(const item_def &it)
    : cls(SH_ITEM),
      feat(DNGN_UNSEEN),
      item(_item_to_show_code(it)),
      mons(MONS_NO_MONSTER),
      colour(0)
{
}

show_type::show_type(show_item_type t)
    : cls(SH_ITEM),
      feat(DNGN_UNSEEN),
      item(t),
      mons(MONS_NO_MONSTER),
      colour(0)
{
}

bool show_type::operator < (const show_type &other) const
{
    if (cls < other.cls)
        return false;
    else if (cls > other.cls)
        return true;
    switch (cls)
    {
    case SH_FEATURE:
        return feat < other.feat;
    case SH_ITEM:
        return item < other.item;
    case SH_MONSTER:
        return mons < other.mons;
    default:
        return false;
    }
}

// Returns true if this is a monster that can be hidden for ^C
// purposes. All non-immobile monsters are removed from the map
// when the clear-map command (^C) is used.
bool show_type::is_cleanable_monster() const
{
    return cls == SH_MONSTER && !mons_class_is_stationary(mons);
}

static void _update_feat_at(const coord_def &gp)
{
    if (!you.see_cell(gp))
        return;

    dungeon_feature_type feat = grd(gp);
    if (feat == DNGN_UNDISCOVERED_TRAP)
        feat = DNGN_FLOOR;
    unsigned colour = env.grid_colours(gp);
    trap_type trap = TRAP_UNASSIGNED;
    if (feat_is_trap(feat))
        trap = get_trap_type(gp);

    env.map_knowledge(gp).set_feature(feat, colour, trap);

    if (haloed(gp))
        env.map_knowledge(gp).flags |= MAP_HALOED;

    if (umbraed(gp))
        env.map_knowledge(gp).flags |= MAP_UMBRAED;

    if (silenced(gp))
        env.map_knowledge(gp).flags |= MAP_SILENCED;

    if (liquefied(gp, false))
        env.map_knowledge(gp).flags |= MAP_LIQUEFIED;

    if (orb_haloed(gp))
        env.map_knowledge(gp).flags |= MAP_ORB_HALOED;

    if (quad_haloed(gp))
        env.map_knowledge(gp).flags |= MAP_QUAD_HALOED;

    if (disjunction_haloed(gp))
        env.map_knowledge(gp).flags |= MAP_DISJUNCT;

#if TAG_MAJOR_VERSION == 34
    if (heated(gp))
        env.map_knowledge(gp).flags |= MAP_HOT;
#endif

    if (golden(gp))
        env.map_knowledge(gp).flags |= MAP_GOLDEN;

    if (is_sanctuary(gp))
    {
        if (testbits(env.pgrid(gp), FPROP_SANCTUARY_1))
            env.map_knowledge(gp).flags |= MAP_SANCTUARY_1;
        else if (testbits(env.pgrid(gp), FPROP_SANCTUARY_2))
            env.map_knowledge(gp).flags |= MAP_SANCTUARY_2;
    }

    if (you.get_beholder(gp))
        env.map_knowledge(gp).flags |= MAP_WITHHELD;

    if (you.get_fearmonger(gp))
        env.map_knowledge(gp).flags |= MAP_WITHHELD;

    if (you.made_nervous_by(gp))
        env.map_knowledge(gp).flags |= MAP_WITHHELD;

    if (feat >= DNGN_STONE_STAIRS_DOWN_I
        && feat <= DNGN_ESCAPE_HATCH_UP
        && is_exclude_root(gp))
    {
        env.map_knowledge(gp).flags |= MAP_EXCLUDED_STAIRS;
    }

    if (is_bloodcovered(gp))
        env.map_knowledge(gp).flags |= MAP_BLOODY;

    if (is_moldy(gp))
    {
        env.map_knowledge(gp).flags |= MAP_MOLDY;
        if (glowing_mold(gp))
            env.map_knowledge(gp).flags |= MAP_GLOWING_MOLDY;
    }

    if (env.level_state & LSTATE_SLIMY_WALL && slime_wall_neighbour(gp))
        env.map_knowledge(gp).flags |= MAP_CORRODING;

    if (emphasise(gp))
        env.map_knowledge(gp).flags |= MAP_EMPHASIZE;

    // Tell the world first.
    dungeon_events.fire_position_event(DET_PLAYER_IN_LOS, gp);

    if (is_notable_terrain(feat))
        seen_notable_thing(feat, gp);

    dgn_seen_vault_at(gp);
}

static show_item_type _item_to_show_code(const item_def &item)
{
    switch (item.base_type)
    {
    case OBJ_ORBS:       return SHOW_ITEM_ORB;
    case OBJ_WEAPONS:    return SHOW_ITEM_WEAPON;
    case OBJ_MISSILES:   return SHOW_ITEM_MISSILE;
    case OBJ_ARMOUR:     return SHOW_ITEM_ARMOUR;
    case OBJ_WANDS:      return SHOW_ITEM_WAND;
    case OBJ_FOOD:       return SHOW_ITEM_FOOD;
    case OBJ_SCROLLS:    return SHOW_ITEM_SCROLL;
    case OBJ_JEWELLERY:
        return jewellery_is_amulet(item) ? SHOW_ITEM_AMULET : SHOW_ITEM_RING;
    case OBJ_POTIONS:    return SHOW_ITEM_POTION;
    case OBJ_BOOKS:      return SHOW_ITEM_BOOK;
    case OBJ_STAVES:     return SHOW_ITEM_STAVE;
    case OBJ_RODS:       return SHOW_ITEM_STAVE;
    case OBJ_MISCELLANY:
        if (item.sub_type == MISC_RUNE_OF_ZOT)
            return SHOW_ITEM_RUNE;
        else
            return SHOW_ITEM_MISCELLANY;
    case OBJ_CORPSES:    return SHOW_ITEM_CORPSE;
    case OBJ_GOLD:       return SHOW_ITEM_GOLD;
    case OBJ_DETECTED:   return SHOW_ITEM_DETECTED;
    default:             return SHOW_ITEM_ORB; // bad item character
    }
}

void update_item_at(const coord_def &gp, bool detected)
{
    if (!in_bounds(gp))
        return;

    item_def eitem;
    bool more_items = false;

    if (you.see_cell(gp))
    {
        if (you.visible_igrd(gp) != NON_ITEM)
            eitem = mitm[you.visible_igrd(gp)];
        else
            return;

        // monster(mimic)-owned items have link = NON_ITEM+1+midx
        if (eitem.link > NON_ITEM && you.visible_igrd(gp) != NON_ITEM)
            more_items = true;
        else if (eitem.link < NON_ITEM && !crawl_state.game_is_arena())
            more_items = true;
    }
    else
    {
        if (detected)
            StashTrack.add_stash(gp.x, gp.y);

        const vector<item_def> stash = item_list_in_stash(gp);
        if (stash.empty())
            return;

        eitem = stash[0];
        if (!detected && stash.size() > 1)
            more_items = true;
    }
    env.map_knowledge(gp).set_item(get_item_info(eitem), more_items);
}

static void _update_cloud(int cloudno)
{
    cloud_struct& cloud = env.cloud[cloudno];
    const coord_def gp = cloud.pos;

    unsigned short ch = 0;

    tileidx_t index = 0;
    if (!cloud.tile.empty())
    {
        if (!tile_main_index(cloud.tile.c_str(), &index))
        {
            mprf(MSGCH_ERROR, "Invalid tile requested for cloud: '%s'.", cloud.tile.c_str());
            ch = TILE_ERROR;
        }
        else
        {
            int offset = tile_main_count(index);
            ch = index + offset;
        }
    }

    int dur = cloud.decay/20;
    if (dur < 0)
        dur = 0;
    else if (dur > 3)
        dur = 3;

    cloud_info ci(cloud.type, get_cloud_colour(cloudno), dur, ch, gp,
                  cloud.killer);
    env.map_knowledge(gp).set_cloud(ci);
}

static void _check_monster_pos(const monster* mons)
{
    int s = mons->mindex();
    ASSERT(mgrd(mons->pos()) == s);

    // [rob] The following in case asserts aren't enabled.
    // [enne] - It's possible that mgrd and mons->x/y are out of
    // sync because they are updated separately.  If we can see this
    // monster, then make sure that the mgrd is set correctly.
    if (mgrd(mons->pos()) != s)
    {
        // If this mprf triggers for you, please note any special
        // circumstances so we can track down where this is coming
        // from.
        mprf(MSGCH_ERROR, "monster %s (%d) at (%d, %d) was "
             "improperly placed.  Updating mgrd.",
             mons->name(DESC_PLAIN, true).c_str(), s,
             mons->pos().x, mons->pos().y);
        mgrd(mons->pos()) = s;
    }
}

/**
 * Determine if a location is valid to present a { glyph.
 *
 * @param where    The location being queried.
 * @param mons     The moster being mimicked.
 * @returns        True if valid, otherwise False.
*/
static bool _valid_invisible_spot(const coord_def &where, const monster* mons)
{
    if (!you.see_cell(where) || where == you.pos()
        || env.map_knowledge(where).flags & MAP_INVISIBLE_UPDATE)
    {
        return false;
    }

    monster *mons_at = monster_at(where);
    if (mons_at && mons_at != mons)
        return false;

    if (monster_habitable_grid(mons, grd(where)))
        return true;

    return false;
}

static int _hashed_rand(const monster* mons, uint32_t id, uint32_t die)
{
    if (die <= 1)
        return 0;

    struct
    {
        uint32_t mid;
        uint32_t id;
        uint32_t seed;
    } data;
    data.mid = mons->mid;
    data.id  = id;
    data.seed = you.attribute[ATTR_SEEN_INVIS_SEED];

    return hash32(&data, sizeof(data)) % die;
}

/**
 * Mark the estimated position of an invisible monster.
 *
 * Marks a spot on the map as possibly containing an unseen monster
 * (showing up as a disturbance in the air).  Also flags the square as
 * updated for invisible monster, which is used by show_init().
 *
 * @param where          The disturbance's map position.
 * @param do_tiles_draw  Trigger a tiles draw of this cell.
**/
static void _mark_invisible_at(const coord_def &where,
                               bool do_tiles_draw = false)
{
    env.map_knowledge(where).set_invisible_monster();
    env.map_knowledge(where).flags |= MAP_INVISIBLE_UPDATE;

    if (do_tiles_draw)
        show_update_at(where);
}

/**
 * Mark monsters that have transitioned from seen to unseen in the last turn get
 * an invisible monster indicator.
 * @param mons      The monster to check.
 * @param hash_ind  The random hash index, combined with the mid to make a
 *                  unique hash for this roll.  Needed for when we can't mark
 *                  the monster's true position and instead mark an adjacent
 *                  one.
*/
static void _handle_unseen_mons(monster* mons, uint32_t hash_ind)
{
    // Monster didn't go unseen last turn.
    if (mons->unseen_pos.origin())
        return;


    // We expire these unseen invis markers after one turn.
    if (you.turn_is_over && !mons->went_unseen_this_turn)
    {
        mons->unseen_pos = coord_def(0, 0);
        return;
    }

    bool do_tiles_draw;
    // Try to use the original position where the monster became unseen.
    if (_valid_invisible_spot(mons->unseen_pos, mons))
    {
        do_tiles_draw = mons->unseen_pos != mons->pos();
        _mark_invisible_at(mons->unseen_pos, do_tiles_draw);
        return;
    }

    // Fall back to a random position adjacent to the unseen position.
    vector <coord_def> adj_unseen;
    for (adjacent_iterator ai(mons->unseen_pos, false); ai; ai++)
    {
        if (_valid_invisible_spot(*ai, mons))
            adj_unseen.push_back(*ai);
    }
    if (adj_unseen.size())
    {
        coord_def new_pos = adj_unseen[_hashed_rand(mons, hash_ind,
                                                    adj_unseen.size())];
        do_tiles_draw = mons->unseen_pos != mons->pos();
        _mark_invisible_at(new_pos, do_tiles_draw);
    }
}

/**
 * Update map knowledge for monsters
 *
 * This function updates the map_knowledge grid with a monster_info if relevant.
 * If the monster is not currently visible to the player, the map knowledge will
 * be upated with a disturbance if necessary.
 * @param mons  The monster at the relevant location.
**/
static void _update_monster(monster* mons)
{
    _check_monster_pos(mons);
    const coord_def gp = mons->pos();

    if (mons->visible_to(&you))
    {
        mons->ensure_has_client_id();
        monster_info mi(mons);
        env.map_knowledge(gp).set_monster(mi);
        return;
    }

    // From here on we're handling an invisible monster, possibly leaving an
    // invisible monster indicator.

    // We cannot use regular randomness here, otherwise redrawing the screen
    // would give out the real position.  We need to save the seed too -- but it
    // needs to be regenerated every turn.
    if (you.attribute[ATTR_SEEN_INVIS_TURN] != you.num_turns)
    {
        you.attribute[ATTR_SEEN_INVIS_TURN] = you.num_turns;
        you.attribute[ATTR_SEEN_INVIS_SEED] = random_int();
    }
    // After the player finishes this turn, the monster's unseen pos (and
    // its invis indicator due to going unseen) will be erased.
    if (!you.turn_is_over)
        mons->went_unseen_this_turn = false;

    // ripple effect?
    if (grd(gp) == DNGN_SHALLOW_WATER
        && !mons_flies(mons)
        && env.cgrid(gp) == EMPTY_CLOUD
        || is_opaque_cloud(env.cgrid(gp))
        && !mons->submerged()
        && !mons->is_insubstantial())
    {
        _mark_invisible_at(gp);
        return;
    }

    // Being submerged is not the same as invisibility.
    if (mons->submerged())
        return;

    int range2 = dist_range(player_monster_detect_radius());
    if (mons->constricted_by == MID_PLAYER
        || (range2 > 1 && (you.pos() - mons->pos()).abs() <= range2))
    {
        _mark_invisible_at(gp);
        return;
    }

    // Monsters at anything other than max stealth get a stealth check; if they
    // fail this and are either very unstealthy or also fail a flat 1/4 chance,
    // they leave an invis indicator at their position.
    if (_hashed_rand(mons, 0, 7) >= mons->stealth() + 4
        && (mons->stealth() <= -2 || !_hashed_rand(mons, 1, 4)))
    {
        _mark_invisible_at(gp);
    }
    else
        _handle_unseen_mons(mons, 2);
}

/**
 * Update map knowledge and set the map tiles at a location.
 * @param gp            The location to update.
 * @param terrain_only  If True, only the feature information/tiles are updated.
**/
void show_update_at(const coord_def &gp, bool terrain_only)
{
    if (you.see_cell(gp))
        env.map_knowledge(gp).clear_data();
    else if (!env.map_knowledge(gp).known())
        return;
    else
        env.map_knowledge(gp).clear_monster();

    // The sequence is grid, items, clouds, monsters.
    _update_feat_at(gp);

    // If there's items on the boundary (shop inventory),
    // we don't show them.
    if (!terrain_only && in_bounds(gp))
    {
        monster* mons = monster_at(gp);
        if (mons && mons->alive())
            _update_monster(mons);
        else if (env.map_knowledge(gp).flags & MAP_INVISIBLE_UPDATE)
            _mark_invisible_at(gp);

        const int cloud = env.cgrid(gp);
        if (cloud != EMPTY_CLOUD && env.cloud[cloud].type != CLOUD_NONE
            && env.cloud[cloud].pos == gp)
        {
            _update_cloud(cloud);
        }

        update_item_at(gp);
    }

#ifdef USE_TILE
    tile_draw_map_cell(gp, true);
#endif
#ifdef USE_TILE_WEB
    tiles.mark_for_redraw(gp);
#endif
}

void show_init(bool terrain_only)
{
    clear_terrain_visibility();
    if (crawl_state.game_is_arena())
    {
        for (rectangle_iterator ri(crawl_view.vgrdc, LOS_MAX_RANGE); ri; ++ri)
        {
            show_update_at(*ri, terrain_only);
            // Invis indicators and update flags not used in Arena.
            env.map_knowledge(*ri).flags &= ~MAP_INVISIBLE_UPDATE;
        }
        return;
    }

    vector <coord_def> update_locs;
    for (radius_iterator ri(you.pos(), you.xray_vision ? LOS_NONE : LOS_DEFAULT); ri; ++ri)
    {
        show_update_at(*ri, terrain_only);
        update_locs.push_back(*ri);
    }

    // Need to clear these update flags now so they don't persist.
    for (unsigned int i = 0; i < update_locs.size(); i++)
        env.map_knowledge(update_locs[i]).flags &= ~MAP_INVISIBLE_UPDATE;
}

// Emphasis may change while off-level (precisely, after
// taking stairs and saving the level, when we reach
// the next level). This catches up.
// It should be equivalent to looping over the whole map
// and setting MAP_EMPHASIZE for any coordinate with
// emphasise(p) == true, but we optimise a bit.
void show_update_emphasis()
{
    // The only thing that can change is that previously unknown
    // stairs are now known. (see is_unknown_stair(), emphasise())
    LevelInfo& level_info = travel_cache.get_level_info(level_id::current());
    vector<stair_info> stairs = level_info.get_stairs();
    for (unsigned i = 0; i < stairs.size(); ++i)
        if (stairs[i].destination.is_valid())
            env.map_knowledge(stairs[i].position).flags &= ~MAP_EMPHASIZE;
}
