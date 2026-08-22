#include "tamagometer_catalog.h"

#include <stdio.h>

static const char *const connection_items[] = {
    "Scone",
    "Sushi",
    "Bread",
    "Cereal",
    "Omelet",
    "Milk",
    "Hamburger",
    "BBQ",
    "Sandwich",
    "Beef Bowl",
    "Cheese",
    "Pizza",
    "Steak",
    "Taco",
    "Sausage on stick",
    "Hot Dog",
    "Pasta",
    "Corn",
    "Turkey",
    "Noodle",
    "Fried Chicken",
    "Waffle",
    "Choco Bar",
    "Escargot",
    "Octopus Sausage",
    "Chikuwa",
    "Rice Ball",
    "Curry",
    "Kobu Maki",
    "Umeboshi",
    "Natto",
    "Fried Shrimp",
    "Takoyaki",
    "Oyster",
    "Naruto",
    "Pigs Feet",
    "Cone",
    "Pudding",
    "Cake",
    "Apple",
    "Sundae",
    "Banana",
    "Fries",
    "Roll Cake",
    "Cupcake",
    "Fruit Juice",
    "Ice Cream",
    "Cheese Cake",
    "Apple Pie",
    "Energy Drink",
    "Corn Dog",
    "Donut",
    "Soda",
    "Popcorn",
    "Pear",
    "Pineapple",
    "Melon",
    "Grapes",
    "Chocolate Heart",
    "Cookie",
    "Whole Cake",
    "Yogurt",
    "Lollipop",
    "Candy",
    "Crepe Suzette",
    "Cherry",
    "Biscuit",
    "Marron Cake",
    "Cream Puff",
    "Gum",
    "Dango",
    "Shaved Ice",
    "Sweet Potato",
    "Mochi",
    "Peanuts",
    "Toast",
    "Crackers",
    "Water",
    "Ball",
    "Pencil",
    "Wig",
    "Sunglasses",
    "RC Car 1",
    "Pen",
    "Weights",
    "RC Car 2 (duck)",
    "RC Car 3",
    "Bow",
    "Darts",
    "Building Block",
    "Cap",
    "Bow Tie",
    "Wings",
    "Hair Gel",
    "Clock",
    "Chest",
    "Phonograph",
    "Fishing Pole",
    "Mirror",
    "Make Up",
    "Boom Box",
    "Music Disc",
    "Shirt",
    "Shoes",
    "Ticket 1",
    "Ticket 2",
    "Ticket 3",
    "Ticket 4",
    "Ticket 5",
    "Doll 1",
    "Umbrella",
    "Lamp",
    "Roller Blades",
    "Action Figure",
    "Stuffed Tama 1",
    "Stuffed Tama 2",
    "Trumpet",
    "Drum",
    "Throne",
    "Music",
    "Plant",
    "Shovel",
    "TV",
    "Honey",
    "Royal Costume",
    "! ! (Clone)",
    "Balloon",
    "Rope",
    "Doll 2",
    "Tama Drink",
    "Castle",
    "Shaver",
    "Cone (animation)",
    "Flower (animation)",
    "Poop (animation)",
    "Jack in the Box",
    "Cake (animation)",
    "Heart (animation)",
    "Snake (animation)",
    "Nothing",
    "Ghost (animation)",
    "Sickness",
    "Passport",
    "Key",
    "Key 2",
    "Map",
    "Book",
    "Laptop",
    "Medal",
    "Cell Phone",
    "Bicycle",
    "Skis Souvenir",
    "Island Souvenir",
    "Surfboard Souvenir",
    "Panda Souvenir",
    "Maracas Souvenir",
    "Diamond Ring",
    "Cape",
    "Crown",
    "Skateboard",
    "3 Balloons",
    "Baseball Cap",
    "Teddy Bear",
    "Rare CD",
    "Rare Shoes",
    "Poster 1",
    "Poster 2",
    "Poster 3",
    "Microphone",
    "Suitcase",
    "Trophy",
    "Famous Picture",
    "Small Crown",
    "Glasses",
    "Sword",
    "Camera",
    "Heart Key",
    "Sparkly Heart",
    "Sparkly Star",
    "M Ball",
    "Heart Ring",
};

static const uint8_t friends_ids[] = {
    0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0,  1,  2,  3,  4,  5,  6,  7,
    8,    9,    10,   11,   12,   13, 14, 15, 16, 17, 18, 19, 20,
    21,   22,   23,   24,   25,   26, 27, 28, 29, 30, 31, 32, 33,
    34,   35,   36,   37,   38,   39, 40, 41, 42, 43, 44, 45, 46,
    47,   48,   49,   50,   51,   52, 53, 54, 55, 56, 57, 58, 59,
};

static const char *const friends_points[] = {
    "1,000 Points - cherries", "800 Points - flowers", "600 Points - music",
    "400 Points - stars",      "200 Points - hearts",
};

static const TamaCategory connection_categories[] = {
    TamaCategoryFavorites,        TamaCategoryRecent,    TamaCategoryFood,
    TamaCategorySnacks,           TamaCategoryItemsToys, TamaCategoryAnimations,
    TamaCategorySouvenirsSpecial,
};

static const TamaCategory friends_categories[] = {
    TamaCategoryFavorites,
    TamaCategoryRecent,
    TamaCategoryJewelry,
    TamaCategoryGotchiPoints,
};

size_t tama_catalog_item_count(TamaMode mode) {
  return mode == TamaModeFriends
             ? sizeof(friends_ids)
             : sizeof(connection_items) / sizeof(connection_items[0]);
}

uint8_t tama_catalog_item_id(TamaMode mode, size_t index) {
  return mode == TamaModeFriends ? friends_ids[index] : (uint8_t)index;
}

const char *tama_catalog_item_name(TamaMode mode, uint8_t item_id, char *buffer,
                                   size_t size) {
  if (mode == TamaModeConnection) {
    return item_id < sizeof(connection_items) / sizeof(connection_items[0])
               ? connection_items[item_id]
               : "Unknown";
  }
  if (item_id >= 0xFB)
    return friends_points[0xFF - item_id];
  if (item_id < 60) {
    snprintf(buffer, size, "Jewelry #%02u", (unsigned int)item_id + 1U);
    return buffer;
  }
  return "Unknown";
}

TamaCategory tama_catalog_item_category(TamaMode mode, uint8_t item_id) {
  if (mode == TamaModeFriends)
    return item_id >= 0xFB ? TamaCategoryGotchiPoints : TamaCategoryJewelry;
  if (item_id <= 35)
    return TamaCategoryFood;
  if (item_id <= 77)
    return TamaCategorySnacks;
  if (item_id <= 131)
    return TamaCategoryItemsToys;
  if (item_id <= 141)
    return TamaCategoryAnimations;
  return TamaCategorySouvenirsSpecial;
}

bool tama_catalog_category_matches(TamaMode mode, TamaCategory category,
                                   uint8_t item_id) {
  return category == TamaCategoryAll ||
         tama_catalog_item_category(mode, item_id) == category;
}

const TamaCategory *tama_catalog_categories(TamaMode mode, size_t *count) {
  if (mode == TamaModeFriends) {
    *count = sizeof(friends_categories) / sizeof(friends_categories[0]);
    return friends_categories;
  }
  *count = sizeof(connection_categories) / sizeof(connection_categories[0]);
  return connection_categories;
}

const char *tama_catalog_category_name(TamaCategory category) {
  switch (category) {
  case TamaCategoryAll:
    return "All";
  case TamaCategoryFavorites:
    return "Favorites";
  case TamaCategoryRecent:
    return "Recently sent";
  case TamaCategoryFood:
    return "Food";
  case TamaCategorySnacks:
    return "Snacks";
  case TamaCategoryItemsToys:
    return "Items & Toys";
  case TamaCategoryAnimations:
    return "Animations";
  case TamaCategorySouvenirsSpecial:
    return "Souvenirs & Special";
  case TamaCategoryJewelry:
    return "Jewelry";
  case TamaCategoryGotchiPoints:
    return "Gotchi Points";
  default:
    return "Unknown";
  }
}

const char *tama_catalog_mode_name(TamaMode mode) {
  return mode == TamaModeFriends ? "Friends - LF RFID" : "Connection - IR";
}

_Static_assert(sizeof(connection_items) / sizeof(connection_items[0]) == 181,
               "Connection catalog must contain 181 items");
_Static_assert(sizeof(friends_ids) == 65,
               "Friends catalog must contain 65 rewards");
