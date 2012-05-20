#include <allegro5/tiled.h>
#include "internal.h"

// Bits on the far end of the 32-bit global tile ID are used for tile flags
const unsigned FLIPPED_HORIZONTALLY_FLAG = 0x80000000;
const unsigned FLIPPED_VERTICALLY_FLAG   = 0x40000000;
const unsigned FLIPPED_DIAGONALLY_FLAG   = 0x20000000;

/*
 * Small workaround for Allegro's list creation.
 * _al_list_create_static() doesn't work for lists of size 0.
 */
static inline _AL_LIST *create_list(size_t capacity)
{
	if (capacity == 0)
		return _al_list_create();
	else
		return _al_list_create_static(capacity);
}

/*
 * Decodes map data from a <data> node
 */
static int decode_layer_data(xmlNode *data_node, TILED_MAP_LAYER *layer)
{
	// TODO: get the encoding and compression
	char *str = trim((char *)data_node->children->content);
	char *data = NULL;
	int flen = 0;
	FILE *tmp = tmpfile();

	decompress(str, tmp);
	fflush(tmp);

	// get file length
	flen = ftell(tmp);

	// read in the file
	rewind(tmp);
	data = (char *)calloc(flen, sizeof(char));
	if (fread(data, sizeof(char), flen, tmp) < flen) {
		fprintf(stderr, "failed to read in map data\n");
		return 1;
	}

	// every tile id takes 4 bytes
	layer->datalen = flen/4;
	layer->data = (char *)calloc(layer->datalen, sizeof(char));
	int x, y, i;
	i = 0;
	for (y = 0; y<layer->height; y++) {
		for (x = 0; x<layer->width; x++) {
			int tileid = 0;
			tileid |= data[i];
			tileid |= data[i+1] << 8;
			tileid |= data[i+2] << 16;
			tileid |= data[i+3] << 24;

			// Read out the flags
			// TODO: implement this
			/*
			bool flipped_horizontally = (tileid & FLIPPED_HORIZONTALLY_FLAG);
			bool flipped_vertically = (tileid & FLIPPED_VERTICALLY_FLAG);
			bool flipped_diagonally = (tileid & FLIPPED_DIAGONALLY_FLAG);
			*/

			// Clear the flags
			tileid &= ~(FLIPPED_HORIZONTALLY_FLAG
			           |FLIPPED_VERTICALLY_FLAG
			           |FLIPPED_DIAGONALLY_FLAG);

			layer->data[i/4] = tileid;
			i += 4;
		}
	}
	//	printf("layer dimensions: %dx%d, data length = %d\n", 
	//			layer->width, layer->height, flen);

	fclose(tmp);
	free(data);
	return 0;
}

/*
 * After all the tiles have been parsed out of their tilesets,
 * create the map's global list of tiles.
 */
static void cache_tile_list(TILED_MAP *map)
{
	map->tiles = create_list(0);
	_AL_LIST_ITEM *tileset_item = _al_list_front(map->tilesets);

	while (tileset_item != NULL) {
		TILED_MAP_TILESET *tileset_ob = _al_list_item_data(tileset_item);
		_AL_LIST_ITEM *tile_item = _al_list_front(tileset_ob->tiles);

		while (tile_item != NULL) {
			TILED_MAP_TILE *tile_ob = _al_list_item_data(tile_item);
			// this is a cache, so don't specify a destructor
			// it will get cleaned up with the associated tileset
			_al_list_push_back(map->tiles, tile_ob);
			tile_item = _al_list_next(tileset_ob->tiles, tile_item);
		}

		tileset_item = _al_list_next(map->tilesets, tileset_item);
	}
}

/*
 * Parses a map file
 * Given the path to a map file, returns a new map struct
 * The struct must be freed once it's done being used
 */
TILED_MAP *tiled_parse_map(const char *dir, const char *filename)
{
	xmlDoc *doc;
	xmlNode *root;
	TILED_MAP *map;

	unsigned i, j, k;

	ALLEGRO_PATH *cwd = al_get_standard_path(ALLEGRO_RESOURCES_PATH);
	ALLEGRO_PATH *path = al_create_path(dir);

	al_join_paths(cwd, path);
	if (!al_change_directory(al_path_cstr(cwd, ALLEGRO_NATIVE_PATH_SEP))) {
		printf("Failed to change directory.");
	}

	al_destroy_path(cwd);
	al_destroy_path(path);

	// Read in the data file
	doc = xmlReadFile(filename, NULL, 0);
	if (!doc) {
		fprintf(stderr, "failed to parse map data: %s\n", filename);
		return NULL;
	}

	// Get the root element, <map>
	root = xmlDocGetRootElement(doc);

	// Get some basic info
	map = (TILED_MAP*)malloc(sizeof(TILED_MAP));
	map->x = 0;
	map->y = 0;
	map->width = atoi(get_xml_attribute(root, "width"));
	map->height = atoi(get_xml_attribute(root, "height"));
	map->tile_width = atoi(get_xml_attribute(root, "tilewidth"));
	map->tile_height = atoi(get_xml_attribute(root, "tileheight"));
	map->orientation = copy(get_xml_attribute(root, "orientation"));

	map->bounds = (TILED_MAP_BOUNDS*)malloc(sizeof(TILED_MAP_BOUNDS));
	map->bounds->left = 0;
	map->bounds->top = 0;
	map->bounds->right = map->width * map->tile_width;
	map->bounds->bottom = map->height * map->tile_height;

	// Get the tilesets
	_AL_LIST *tilesets = get_children_for_name(root, "tileset");
	map->tilesets = create_list(_al_list_size(tilesets));

	_AL_LIST_ITEM *tileset_item = _al_list_front(tilesets);
	while (tileset_item) {
		xmlNode *tileset_node = (xmlNode*)_al_list_item_data(tileset_item);
		TILED_MAP_TILESET *tileset_ob = (TILED_MAP_TILESET*)malloc(sizeof(TILED_MAP_TILESET));
		tileset_ob->firstgid = atoi(get_xml_attribute(tileset_node, "firstgid"));
		tileset_ob->tilewidth = atoi(get_xml_attribute(tileset_node, "tilewidth"));
		tileset_ob->tileheight = atoi(get_xml_attribute(tileset_node, "tileheight"));
		tileset_ob->name = copy(get_xml_attribute(tileset_node, "name"));

		// Get this tileset's image
		xmlNode *image_node = get_first_child_for_name(tileset_node, "image");
		tileset_ob->width = atoi(get_xml_attribute(image_node, "width"));
		tileset_ob->height = atoi(get_xml_attribute(image_node, "height"));
		tileset_ob->source = copy(get_xml_attribute(image_node, "source"));
		tileset_ob->bitmap = al_load_bitmap(tileset_ob->source);

		// Get this tileset's tiles
		_AL_LIST *tiles = get_children_for_name(tileset_node, "tile");
		tileset_ob->tiles = create_list(_al_list_size(tiles));

		_AL_LIST_ITEM *tile_item = _al_list_front(tiles);
		while (tile_item) {
			xmlNode *tile_node = (xmlNode*)_al_list_item_data(tile_item);
			TILED_MAP_TILE *tile_ob = (TILED_MAP_TILE*)malloc(sizeof(TILED_MAP_TILE));
			tile_ob->id = tileset_ob->firstgid + atoi(get_xml_attribute(tile_node, "id"));
			tile_ob->tileset = tileset_ob;
			tile_ob->bitmap = NULL;

			// Get this tile's properties
			_AL_LIST *properties = get_children_for_name(
					get_first_child_for_name(tile_node, "properties"),
					"property");
			tile_ob->properties = create_list(_al_list_size(properties));

			_AL_LIST_ITEM *property_item = _al_list_front(properties);
			while (property_item) {
				xmlNode *prop_node = (xmlNode*)_al_list_item_data(property_item);
				TILED_MAP_TILE_PROPERTY *prop_ob = (TILED_MAP_TILE_PROPERTY*)malloc(sizeof(TILED_MAP_TILE_PROPERTY));
				prop_ob->name = copy(get_xml_attribute(prop_node, "name"));
				prop_ob->value = copy(get_xml_attribute(prop_node, "value"));
				_al_list_push_back_ex(tile_ob->properties, prop_ob, dtor_tile_prop);
				property_item = _al_list_next(properties, property_item);
			}

			_al_list_push_back_ex(tileset_ob->tiles, tile_ob, dtor_map_tile);
			tile_item = _al_list_next(tiles, tile_item);
		}

		_al_list_destroy(tiles);
		_al_list_push_back_ex(map->tilesets, tileset_ob, dtor_map_tileset);
		tileset_item = _al_list_next(tilesets, tileset_item);
	}

	_al_list_destroy(tilesets);

	// Create the map's master list of tiles
	cache_tile_list(map);

	// Get the layers
	_AL_LIST *layers = get_children_for_name(root, "layer");
	map->layers = create_list(_al_list_size(layers));

	_AL_LIST_ITEM *layer_item = _al_list_front(layers);
	while (layer_item) {
		xmlNode *layer_node = _al_list_item_data(layer_item);
		TILED_MAP_LAYER *layer_ob = (TILED_MAP_LAYER*)malloc(sizeof(TILED_MAP_LAYER));
		layer_ob->name = copy(get_xml_attribute(layer_node, "name"));
		layer_ob->width = atoi(get_xml_attribute(layer_node, "width"));
		layer_ob->height = atoi(get_xml_attribute(layer_node, "height"));
		layer_ob->map = map;

		char *layer_visible = get_xml_attribute(layer_node, "visible");
		layer_ob->visible = (layer_visible != NULL ? atoi(layer_visible) : 1);

		char *layer_opacity = get_xml_attribute(layer_node, "opacity");
		layer_ob->opacity = (layer_opacity != NULL ? atof(layer_opacity) : 1.0);

		decode_layer_data(get_first_child_for_name(layer_node, "data"), layer_ob);

		// Create any missing tile objects
		for (j = 0; j<layer_ob->height; j++) {
			for (k = 0; k<layer_ob->width; k++) {
				char id = tile_id(layer_ob, k, j);

				if (id == 0)
					continue;

				TILED_MAP_TILE *tile_ob = tiled_get_tile_for_id(map, id);
				if (!tile_ob) {
					// wasn't defined in the map file, presumably
					// because it had no properties
					tile_ob = (TILED_MAP_TILE*)malloc(sizeof(TILED_MAP_TILE));
					tile_ob->id = id;
					tile_ob->properties = _al_list_create();
					tile_ob->tileset = NULL;
					tile_ob->bitmap = NULL;

					// locate its tilemap
					_AL_LIST_ITEM *tileset_item = _al_list_front(map->tilesets);
					while (tileset_item != NULL) {
						TILED_MAP_TILESET *tileset_ob = _al_list_item_data(tileset_item);
						if (tileset_ob->firstgid <= id) {
							if (!tile_ob->tileset || tileset_ob->firstgid > tile_ob->tileset->firstgid) {
								tile_ob->tileset = tileset_ob;
							}
						}
						tileset_item = _al_list_next(map->tilesets, tileset_item);
					}

					_al_list_push_back_ex(map->tiles, tile_ob, dtor_map_tile);
				}

				// create this tile's bitmap if it hasn't been yet
				if (!tile_ob->bitmap) {
					TILED_MAP_TILESET *tileset = tile_ob->tileset;
					int id = tile_ob->id - tileset->firstgid;
					int width = tileset->width / tileset->tilewidth;
					int x = (id % width) * tileset->tilewidth;
					int y = (id / width) * tileset->tileheight;
					tile_ob->bitmap = al_create_sub_bitmap(
							tileset->bitmap,
							x, y,
							tileset->tilewidth,
							tileset->tileheight);
				}
			}
		}

		_al_list_push_back_ex(map->layers, layer_ob, dtor_map_layer);
		layer_item = _al_list_next(layers, layer_item);
	}

	_al_list_destroy(layers);

	// Done parsing XML, so let's free the doc
	xmlFreeDoc(doc);

	// Create the map's backbuffer
	ALLEGRO_BITMAP *orig_backbuffer = al_get_target_bitmap();
	map->backbuffer = al_create_bitmap(map->bounds->right, map->bounds->bottom);
	map->bounds->right -= al_get_bitmap_width(orig_backbuffer);
	map->bounds->bottom -= al_get_bitmap_height(orig_backbuffer);
	al_set_target_bitmap(map->backbuffer);

	if (!strcmp(map->orientation, "orthogonal")) {
		_AL_LIST_ITEM *layer_item = _al_list_front(map->layers);
		while (layer_item != NULL) {
			TILED_MAP_LAYER *layer_ob = _al_list_item_data(layer_item);
			for (i = 0; i<layer_ob->height; i++) {
				for (j = 0; j<layer_ob->width; j++) {
					char id = tile_id(layer_ob, j, i);
					TILED_MAP_TILE *tile_ob = tiled_get_tile_for_id(map, id);
					if (!tile_ob)
						continue;

					int tx = j*(tile_ob->tileset->tilewidth);
					int ty = i*(tile_ob->tileset->tileheight);

					al_draw_bitmap(tile_ob->bitmap, tx, ty, 0);
				}
			}

			layer_item = _al_list_next(map->layers, layer_item);
		}
	}
	else {
		fprintf(stderr, "Cannot draw unsupported map orientation: %s\n", map->orientation);
	}

	al_set_target_bitmap(orig_backbuffer);
	
	return map;
}
