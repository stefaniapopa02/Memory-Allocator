// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "helpers.h"

struct block_meta *head = NULL;  
struct block_meta *last = NULL;
struct block_meta *new ;
struct block_meta *head_sbrk = NULL;


void *alloc(size_t size, int type){ //type = 1 => malloc ; type = 0 => calloc

	void *payld_to_return;
	struct block_meta *iter;


	if(size == 0){
		return NULL;
	}

//............................................................PADDING.....................................................................

	//padding pt struct block_meta
		int size_meta = sizeof(struct block_meta);
		int padd_meta;
		int next_8_multiple;

		if(size_meta % 8 == 0){
			padd_meta = 0;
		}else{
			next_8_multiple = 8 * (size_meta / 8 + 1);
			padd_meta = next_8_multiple - size_meta;
		}

		//padding pt payload
		int size_payload = size;
		int padd_payld;
		int next_8_multiple_payld;

		if(size_payload % 8 == 0){
			padd_payld = 0;
		}else{
			next_8_multiple_payld = 8 * (size_payload / 8 + 1);
			padd_payld = next_8_multiple_payld - size_payload;
		}

		int total_block_size = size_meta + padd_meta + size_payload + padd_payld;

		//size-ul minim pt un bloc de memorie
		int min_payload = 8; //(payload-ul are cel putin un octet + padding pana la 8 octeti)
		int min_block_size = size_meta + padd_meta + min_payload;


	//stabilirea dimensiunii limita in functie de tipul de alocare (malloc sau calloc)

	size_t limit;
	if(type == 1){ //malloc
		limit = MMAP_THRESHOLD;
	}else{ //calloc
		limit = getpagesize();
	}



	//..................................................PREALOCARE.............................................
	//prima data cand vreau sa aloc memorie cu size < limit => aloc un bloc de dimensiune MMAP_TRESHOLD pt optimizarea apelurilor de sistem
	if(total_block_size < (int)limit && head_sbrk == NULL){
		
		head_sbrk = sbrk(MMAP_THRESHOLD);
		head_sbrk->size = MMAP_THRESHOLD;
		head_sbrk->status = STATUS_ALLOC;
		head_sbrk->next = NULL;

		if(head != NULL){
			last->next = head_sbrk;
		}

		if(head == NULL){
            head = head_sbrk;
        }

		last = head_sbrk;

		payld_to_return = (void*)head_sbrk + size_meta + padd_meta;

		if(type == 0){
			memset(payld_to_return, 0 , size);
		}

		return payld_to_return;
		
	}

		
	


	//...................................................UNIREA BLOCURILOR FREE DE MEMORIE......................................................
	//pornind cu inceputul zonei continue de memorie, unim toate blocurile free consecutive
	iter = head; 
	struct block_meta *next;

	while(iter != NULL && iter->next != NULL){
		next = iter->next;

		if(iter->status == STATUS_FREE && next->status == STATUS_FREE){
			iter->size = iter->size + next->size;
			iter->next = next->next;

			if(next == last){
				last = iter;
			}
			
		}else{ //nu am 2 blocuri de unit
			iter = iter->next;
		}

	}


	//.................................................REFOLOSIREA BLOCURILOR DE MEMORIE FREE..........................................
	//caut blocul free cu dimensiunea cea mai mica, dar > total_block_size

	struct block_meta *free_min_size = NULL;;
	int min_size = INT_MAX;
	iter = head;

	while(iter != NULL){
		if(iter->status == STATUS_FREE){
			if((int)iter->size >= total_block_size && (int)iter->size < min_size){
				free_min_size = iter;
				min_size = iter->size;
			}
		}
		iter = iter->next;
	}

	//am gasit bloc de memorie free

	// if(min_size != INT_MAX){
	if(free_min_size != NULL){
		//verific daca este suficient de mare incat sa il il impart in 2 blocuri, pentru a putea folosi spatiul ramas si a mai face inca un malloc
		if((int)free_min_size->size >= total_block_size + min_block_size){
			struct block_meta * splitted; //nou nod de inserat pt zona de mem ramasa libera dupa ocuparea celor size octeti
			splitted = (void *)free_min_size + total_block_size;

			splitted->size = free_min_size->size - total_block_size;
			splitted->status = STATUS_FREE;
			splitted->next = free_min_size->next;

			free_min_size->size = total_block_size;
			free_min_size->status = STATUS_ALLOC;
			free_min_size->next = splitted;

			if(last == free_min_size){
				last = splitted;
			}

		}else{
			free_min_size->status = STATUS_ALLOC;
		}

		//returnam pointer catre payload, nu catre inceputul zonei de stocare a metadatelor
		payld_to_return = (void*)free_min_size + size_meta + padd_meta;

		if(type == 0){ //calloc
			memset(payld_to_return, 0, size);
		}

		return payld_to_return;


	}else{	//nu am gasit bloc de memorie free


		//daca ultimul nod din lista reprezinta o zona de memorie libera, aloc cata memorie lipseste pentru  payload-ul meu de size octeti
		if(last != NULL && last->status == STATUS_FREE){
			sbrk(total_block_size - last->size);
			
			last->size = total_block_size;
			last->status = STATUS_ALLOC;

			payld_to_return = (void*)last + size_meta + padd_meta;

			if(type == 0) {
                memset(payld_to_return, 0, size);
            }

			return	payld_to_return;

		}
		



		//.............................................ALOCARE CU SBRK SAU MMAP(in fct de size)........................................................

		if(total_block_size < (int)limit){

			new = sbrk(total_block_size);
			new->status = STATUS_ALLOC;
			new->size = total_block_size;

		}else{

			new = mmap(NULL, total_block_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			new->status = STATUS_MAPPED;
			new->size = total_block_size;

			printf("Intra unde trb\n");

		}


		//................................................ADAUGAREA ELEMENTULUI IN LISTA..................................................................
		//adaug la inceput nodurile ce reprezinta zone de memorie alocate cu MMAP si la sfarsit pe cele continue, alocate cu SBRK

		if(head == NULL){ //este primul element din lista
			head = new;
			head->next = NULL;
			last = head;

		}else{ //nu este primul el din lista

			//zona de memorie alocata cu SBRK => adaug nodul la sfarsit, pastrand o zona continua de memorie
			if(new->status == STATUS_ALLOC){
				last->next = new;
				new->next = NULL;

				//comentat
				// if(last->status == STATUS_MAPPED){ //=> de aici incep nodurile alocate cu SBRK, ce reprezinta o zona continua de memorie
				// 	head_sbrk = new;
				// }

				last = new;

			}

			//zona de memorie alocata cu MMAP => adaug nodul la inceput, langa celelalte noduri ce reprezinat zone discontinue de memorie
			if(new->status == STATUS_MAPPED){
				new->next = head;
				head = new;
				
			}
			
		}

		payld_to_return = (void*)new + size_meta + padd_meta;

		if(type == 0){ //calloc
			memset(payld_to_return, 0, size);
		}

		return payld_to_return;

	}

	return NULL;

}


void *os_malloc(size_t size)
{
	/* TODO: Implement os_malloc */

	return alloc(size, 1);
}

void os_free(void *ptr)
{
	/* TODO: Implement os_free */

	if(ptr == NULL){
		return;
	}

	struct block_meta *meta_payld = NULL; //pointer la zona de memorie in care sunt stocate metadatele, urmate de payload-ul corespunzator

	//padding pt struct block_meta
		int size_meta = sizeof(struct block_meta);
		int padd_meta;
		int next_8_multiple;

		if(size_meta % 8 == 0){
			padd_meta = 0;
		}else{
			next_8_multiple = 8 * (size_meta / 8 + 1);
			padd_meta = next_8_multiple - size_meta;
		}

	//cautam in lista elementul ce reprezinta zona de memorie in care payload-ul este corespunzator lui ptr
	struct block_meta *iter = head;

	while(iter != NULL){
		void *payld = (void*)iter + size_meta + padd_meta;
		if(payld == ptr){
			break;
		}
		iter = iter->next;
	}
	meta_payld = iter;

	if(meta_payld == NULL){ //nu a gasit payload-ul de la adresa ptr
		return;
	}

	if(meta_payld->status == STATUS_ALLOC){ // daca blocul de memorie a fost alocat cu SBRK, nu trebuie eliminat nodul, ci doar modificat statusul
		meta_payld->status = STATUS_FREE;
		return;

	}

	if(meta_payld->status == STATUS_MAPPED){ //bloc de memorie alocat cu MMAP => nodul trebuie eliminat

	
		if(head == meta_payld){//daca este primul nod din lista
			if(head == last) {
                last = NULL;
            }

			head = meta_payld->next;
			munmap(meta_payld, meta_payld->size);
			return;

		}else{
			//pentru a putea elimina nodul, cautam nodul anterior al acestuia
			struct block_meta *curr = head->next;
			struct block_meta *prev = head;

			while(curr != NULL){
				if(curr == meta_payld){
					break;
				}else{
					prev = curr;
					curr = curr->next; 
				}
			}
			prev->next = curr->next;
			munmap(curr, curr->size);

			return;

		}

	}

}

void *os_calloc(size_t nmemb, size_t size)
{
	/* TODO: Implement os_calloc */

	return alloc(nmemb * size, 0);
	
}

void *os_realloc(void *ptr, size_t size)
{
	/* TODO: Implement os_realloc */


	//............................................................PADDING.....................................................................

	//padding pt struct block_meta
		int size_meta = sizeof(struct block_meta);
		int padd_meta;
		int next_8_multiple;

		if(size_meta % 8 == 0){
			padd_meta = 0;
		}else{
			next_8_multiple = 8 * (size_meta / 8 + 1);
			padd_meta = next_8_multiple - size_meta;
		}

		//padding pt payload
		int size_payload = size;
		int padd_payld;
		int next_8_multiple_payld;

		if(size_payload % 8 == 0){
			padd_payld = 0;
		}else{
			next_8_multiple_payld = 8 * (size_payload / 8 + 1);
			padd_payld = next_8_multiple_payld - size_payload;
		}

		int total_block_size = size_meta + padd_meta + size_payload + padd_payld;

		//size-ul minim pt un bloc de memorie
		int min_payload = 8; //(payload-ul are cel putin un octet + padding pana la 8 octeti)
		int min_block_size = size_meta + padd_meta + min_payload;


	void *payld_to_return = NULL;

	if(ptr == NULL){
		return os_malloc(size);
	}

	if(size == 0){
		os_free(ptr);
		return NULL;
	}

	struct block_meta *meta_payld = NULL;
	struct block_meta *iter = head;

	while(iter != NULL){
		void *payld = (void*)iter + size_meta + padd_meta;
		if(payld == ptr){
			break;
		}
		iter = iter->next;

	}
	meta_payld = iter;

	if(meta_payld == NULL){ //daca nu am gasit nodul corespunzator lui ptr
		return NULL;
	}

	if(meta_payld->status == STATUS_FREE){
		return NULL;
	}



	if(total_block_size > MMAP_THRESHOLD){
		void *new_block = os_malloc(size);
		void *old_payld = (void*)meta_payld + size_meta + padd_meta;
		memcpy(new_block, old_payld, meta_payld->size -size_meta - padd_meta);
		os_free(old_payld);

		return new_block;
	}

	//.........................................................PREALOCARE.................................................................

	if(total_block_size < MMAP_THRESHOLD && head_sbrk == NULL){
		void *new_block = os_malloc(size);

		os_free(ptr);

		return new_block;
	}

	if(total_block_size < (int)meta_payld->size && meta_payld->status== STATUS_ALLOC){ //zona de memorie trebuie sa fie trunchiata
		if((int)meta_payld->size >= total_block_size + min_block_size){ // => zona de memorie suficient de mare incat sa mai ramana octeti pt inca un bloc
			struct block_meta *splitted = (void*)meta_payld + total_block_size;

			splitted->size = meta_payld->size - total_block_size;
			splitted->status = STATUS_FREE;
			splitted->next = meta_payld->next;

			meta_payld->size = total_block_size;
			meta_payld->next = splitted;

			if(last == meta_payld){
				last = splitted;
			}

			payld_to_return = (void*)meta_payld + size_meta + padd_meta;

			return payld_to_return;

		}
	}

	
	if(total_block_size > (int)meta_payld->size){  
		//zona de memorie expandata
		if(meta_payld->next != NULL && meta_payld->next->status == STATUS_FREE){
			struct block_meta *free_blocks = meta_payld->next;
			size_t crt_size = meta_payld->size;

			while(free_blocks != NULL && free_blocks == STATUS_FREE && (int)crt_size < total_block_size){
				crt_size += free_blocks->size;
				free_blocks = free_blocks->next;
			}

			if((int)crt_size >= total_block_size){
				meta_payld->next = free_blocks;
				meta_payld->size = crt_size;

				payld_to_return = (void*)meta_payld + size_meta + padd_meta;

				return payld_to_return;
			}
		}

		//.................................................REFOLOSIREA BLOCURILOR DE MEMORIE FREE..........................................
		//caut blocul free cu dimensiunea cea mai mica, dar > total_block_size

		struct block_meta *free_min_size = NULL;;
		int min_size = INT_MAX;
		iter = head;

		while(iter != NULL){
			if(iter->status == STATUS_FREE){
				if((int)iter->size >= total_block_size && (int)iter->size < min_size){
					free_min_size = iter;
					min_size = iter->size;
				}
			}
			iter = iter->next;
		}

		//am gasit bloc de memorie free

		// if(min_size != INT_MAX)
		if(free_min_size != NULL){
			//verific daca este suficient de mare incat sa il il impart in 2 blocuri, pentru a putea folosi spatiul ramas si a mai face inca un malloc
			if((int)free_min_size->size >= total_block_size + min_block_size){
				struct block_meta * splitted; //nou nod de inserat pt zona de mem ramasa libera dupa ocuparea celor size octeti
				splitted = (void *)free_min_size + total_block_size;

				splitted->size = free_min_size->size - total_block_size;
				splitted->status = STATUS_FREE;
				splitted->next = free_min_size->next;

				free_min_size->size = total_block_size;
				free_min_size->status = STATUS_ALLOC;
				free_min_size->next = splitted;

				if(last == free_min_size){
					last = splitted;
				}

			}else{
				free_min_size->status = STATUS_ALLOC;
			}


			//returnam pointer catre payload, nu catre inceputul zonei de stocare a metadatelor
			payld_to_return = (void*)free_min_size + size_meta + padd_meta;


			memcpy(payld_to_return, ptr, size);

			return payld_to_return;
		}

		void *p = os_malloc(size);
		void *old_payld = (void*)meta_payld + size_meta + padd_meta;
		memcpy(p, old_payld, meta_payld->size -size_meta - padd_meta);
		os_free(old_payld);

		memcpy(p, ptr, size);

		return p;

	}

	return NULL;
}

