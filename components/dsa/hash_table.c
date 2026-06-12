#include "hash_table.h"
#include <string.h>

/**
 * aca uso el algoritmo djb2. Creada por Daniel Bernstein. Es muy rapida 
 * y genera pocas colisiones. Es muy buena, busquenla.
 *
 */
static unsigned long hash_string(const char *str) {
  unsigned long hash = 5381;
  int c;

  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + c; // aca multiplica por 33. 
  }
  return hash;
}

static int get_index(const char *id) {

  return hash_string(id) % HASH_TABLE_SIZE;
}

// para inicializar la tabla, setea todas las celdas en ENTRY_EMPTY (vacias nunca usadas).
void hash_table_init(HashTable *table) {
  if (table == NULL) {
    return;
  }

  table->count = 0;

  for (int pp = 0; pp < HASH_TABLE_SIZE; pp++) {
    table->entries[pp].status = ENTRY_EMPTY;
  }
}

// trivial: sirve para ver si la tabla esta llena. Esta bueno para verificar antes de insertar algo.
bool hash_table_is_full(HashTable *table) {
  if (table == NULL) {
    return false;
  }

  return table->count >= HASH_TABLE_SIZE;

}

// busca un producto por id. probablemente sea la funcion mas importante para el qr.
Product *hash_table_find(HashTable *table, const char *id) {
    if (table == NULL || id == NULL) {
        return NULL;
    }

    int index = get_index(id);

    for (int pp = 0; pp < HASH_TABLE_SIZE; pp++) {
        int probe_index = (index + pp) % HASH_TABLE_SIZE;

        HashEntry *entry = &table->entries[probe_index];

        if (entry->status == ENTRY_EMPTY) {
            return NULL;
        }

        if (entry->status == ENTRY_OCCUPIED &&
            strcmp(entry->product.id, id) == 0) {
            return &entry->product;
        }
    }

    return NULL;
}

// si por ahi inserto un producto que ya existe, se actualiza el mismo.
bool hash_table_insert(HashTable *table, Product product) {
    if (table == NULL || product.id[0] == '\0') {
        return false;
    }

    int index = get_index(product.id);
    int first_deleted_index = -1;

    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        int probe_index = (index + i) % HASH_TABLE_SIZE;

        HashEntry *entry = &table->entries[probe_index];

        if (entry->status == ENTRY_OCCUPIED &&
            strcmp(entry->product.id, product.id) == 0) {
            entry->product = product;
            return true;
        }

        if (entry->status == ENTRY_DELETED && first_deleted_index == -1) {
            first_deleted_index = probe_index;
        }

        if (entry->status == ENTRY_EMPTY) {
            int target_index = first_deleted_index != -1
                                   ? first_deleted_index
                                   : probe_index;

            table->entries[target_index].product = product;
            table->entries[target_index].status = ENTRY_OCCUPIED;
            table->count++;

            return true;
        }
    }

    if (first_deleted_index != -1) {
        table->entries[first_deleted_index].product = product;
        table->entries[first_deleted_index].status = ENTRY_OCCUPIED;
        table->count++;
        return true;
    }

    return false;
}

// de momento no le veo mucho uso pero puede servir se por MQTT se quiere eliminar un producto de la base de datos.
bool hash_table_remove(HashTable *table, const char *id) {
    if (table == NULL || id == NULL) {
        return false;
    }

    int index = get_index(id);

    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        int probe_index = (index + i) % HASH_TABLE_SIZE;

        HashEntry *entry = &table->entries[probe_index];

        if (entry->status == ENTRY_EMPTY) {
            return false;
        }

        if (entry->status == ENTRY_OCCUPIED &&
            strcmp(entry->product.id, id) == 0) {
            entry->status = ENTRY_DELETED;
            table->count--;
            return true;
        }
    }

    return false;
}

// para modificar solo el stock de un producto uu
bool hash_table_update_stock(HashTable *table, const char *id, int new_stock) {
    Product *product = hash_table_find(table, id);

    if (product == NULL) {
        return false;
    }

    product->stock = new_stock;
    return true;
}
