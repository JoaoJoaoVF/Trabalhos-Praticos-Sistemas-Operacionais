#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "fs.h"

#define MAGIC 0xdcc605f5 // esse valor está no fs.h
#define ERROR -1
#define MAX_NAME 100       
#define MAX_SUBFOLDERS 100 
#define MAX_FILE_SIZE 5000 
#define MAX_PATH_NAME 4000 
#define EPS 1e-6           

int is_fs_open = 0;

void search_inode(struct superblock *sb, struct inode *in, struct inode *in2, struct nodeinfo *info2, int k)
{
    lseek(sb->fd, in->links[k] * sb->blksz, SEEK_SET);
    read(sb->fd, in2, sb->blksz);

    if (in2->mode == IMCHILD)
    {
        lseek(sb->fd, in2->parent * sb->blksz, SEEK_SET);
        read(sb->fd, in2, sb->blksz);
    }

    lseek(sb->fd, in2->meta * sb->blksz, SEEK_SET);
    read(sb->fd, info2, sb->blksz);
}

void copy_inode(struct inode *a, struct inode *b, struct nodeinfo *c)
{
    a->mode = b->mode;
    a->parent = b->parent;
    a->meta = b->meta;
    a->next = b->next;

    int i;

    if (b->mode == IMDIR)
    {
        for (i = 0; i < c->size; i++)
        {
            a->links[i] = b->links[i];
        }
    }
}

void copy_nodeinfo(struct nodeinfo *a, struct nodeinfo *b)
{
    a->size = b->size;

    int i;
    for (i = 0; i < 7; i++)
    {
        a->reserved[i] = b->reserved[i];
    }

    strcpy(a->name, b->name);
}

struct superblock *create_superblock(const char *fname, uint64_t blocksize, uint64_t numero_blocos)
{
    struct superblock *sb;
    sb = (struct superblock *)malloc(blocksize);
    sb->magic = MAGIC;
    sb->blks = numero_blocos;
    sb->blksz = blocksize;
    sb->freeblks = numero_blocos - 3;

    /* na posição 0 está o superbloco em si
    na posição 1 está o nodeinfo
    na posição 2 está o diretório raiz
    na posição 3 está a lista de blocos vazios */
    sb->root = 2;     // localização do diretório raiz
    sb->freelist = 3; // localização da lista de blocos vazios
    sb->fd = open(fname, O_RDWR, 0777);
    return sb;
}

struct inode *create_root_directory(uint64_t blocksize)
{
    struct inode *root;
    root = (struct inode *)malloc(blocksize);
    root->mode = IMDIR;
    root->parent = 2;
    root->meta = 1;
    root->next = 0;
    memset(root->links, 0, sizeof(uint64_t));
    return root;
}

/* Build a new filesystem image in =fname (the file =fname should be present
 * in the OS's filesystem).  The new filesystem should use =blocksize as its
 * block size; the number of blocks in the filesystem will be automatically
 * computed from the file size.  The filesystem will be initialized with an
 * empty root directory.  This function returns NULL on error and sets errno
 * to the appropriate error code.  If the block size is smaller than
 * MIN_BLOCK_SIZE bytes, then the format fails and the function sets errno to
 * EINVAL.  If there is insufficient space to store MIN_BLOCK_COUNT blocks in
 * =fname, then the function fails and sets errno to ENOSPC. */
struct superblock *fs_format(const char *fname, uint64_t blocksize)
{

    // verifica se o tamanho do bloco é menor do que o tamanho mínimo de bloco
    if (blocksize < MIN_BLOCK_SIZE)
    {
        errno = EINVAL;
        return NULL;
    }

    FILE *arquivo = fopen(fname, "r");

    if (arquivo == NULL)
        return NULL;

    // fazendo o ponteiro apontar para o final do arquivo
    fseek(arquivo, 0, SEEK_END);
    uint64_t tamanho = ftell(arquivo);
    fclose(arquivo);

    uint64_t numero_blocos = tamanho / blocksize;

    // verifica se o numero de blocos é menor do que o numero mínimo de blocos
    if (numero_blocos < MIN_BLOCK_COUNT)
    {
        errno = ENOSPC;
        return NULL;
    }

    struct superblock *sb = create_superblock(fname, blocksize, numero_blocos);
    struct inode *root = create_root_directory(blocksize);
    struct nodeinfo *rootinfo = (struct nodeinfo *)malloc(blocksize);

    rootinfo->size = 0;
    strcpy(rootinfo->name, "/\0");

    write(sb->fd, sb, blocksize);
    write(sb->fd, rootinfo, blocksize);
    write(sb->fd, root, blocksize);

    struct freepage *fp;
    // inicializando a lista de páginas vazias
    for (int i = 3; i < numero_blocos - 1; i++)
    {
        fp->next = i + 1;
        write(sb->fd, fp, blocksize);
    }

    fp->next = 0;
    write(sb->fd, fp, blocksize);

    return sb;
}

/* Open the filesystem in =fname and return its superblock.  Returns NULL on
 * error, and sets errno accordingly.  If =fname does not contain a
 * 0xdcc605fs, then errno is set to EBADF. */
struct superblock *fs_open(const char *fname)
{
    // Verificar se o sistema de arquivos já está aberto
    if (is_fs_open)
    {
        errno = EBUSY;
        return NULL;
    }

    uint64_t file_descriptor = open(fname, O_RDWR);

    struct superblock *sb = malloc(sizeof(struct superblock));

    lseek(file_descriptor, 0, SEEK_SET);

    read(file_descriptor, sb, sizeof(struct superblock));

    if (sb->magic != MAGIC)
    {
        close(file_descriptor);
        errno = EBADF;
        free(sb);
        return NULL;
    }

    // Marcar o sistema de arquivos como aberto
    is_fs_open = 1;

    return sb;
}

/* Close the filesystem pointed to by =sb.  Returns zero on success and a
 * negative number on error.  If there is an error, all resources are freed
 * and errno is set appropriately. */
int fs_close(struct superblock *sb)
{
    if (sb->magic != MAGIC)
    {
        errno = EBADF;
        return ERROR;
    }

    close(sb->fd);
    free(sb);

    // Marcar o sistema de arquivos como fechado
    is_fs_open = 0;

    return 0;
}

/* Get a free block in the filesystem.  This block shall be removed from the
 * list of free blocks in the filesystem.  If there are no free blocks, zero
 * is returned.  If an error occurs, (uint64_t)-1 is returned and errno is set
 * appropriately. */
uint64_t fs_get_block(struct superblock *sb)
{
    if (sb->freeblks == 0)
    {
        errno = ENOSPC;
        return 0;
    }

    lseek(sb->fd, sb->blksz * sb->freelist, SEEK_SET);

    struct freepage *fp = (struct freepage *)malloc(sb->blksz);
    if (read(sb->fd, fp, sb->blksz) < 0)
    {
        free(fp);
        return ((uint64_t)-1);
    }

    uint64_t freeblock = sb->freelist;
    sb->freelist = fp->next;
    sb->freeblks -= 1;

    lseek(sb->fd, 0, SEEK_SET);
    write(sb->fd, sb, sb->blksz);

    free(fp);

    return freeblock;
}

/* Put =block back into the filesystem as a free block.  Returns zero on
 * success or a negative value on error.  If there is an error, errno is set
 * accordingly.
 * Retorna o bloco de número block para a lista de blocos livres do sistema de arquivo
 * sb. Retorna zero em caso de sucesso e um valor negativo em caso de erro. Código de
 * erro, se ocorrer, é salvo em errno.
 */
int fs_put_block(struct superblock *sb, uint64_t block)
{
    if (sb->magic != MAGIC)
    {
        errno = EBADF;
        return ERROR;
    }

    struct freepage *fp = (struct freepage *)malloc(sb->blksz);
    fp->next = sb->freelist;

    lseek(sb->fd, sb->blksz * block, SEEK_SET);
    if (write(sb->fd, fp, sb->blksz) < 0)
    {
        free(fp);
        return ERROR;
    }

    sb->freelist = block;
    sb->freeblks += 1;

    lseek(sb->fd, 0, SEEK_SET);
    if (write(sb->fd, sb, sb->blksz) < 0)
    {
        free(fp);
        return ERROR;
    }

    free(fp);

    return 0;
}

void jump_to_next_inode(struct superblock *sb, struct inode *in)
{
    lseek(sb->fd, in->next * sb->blksz, SEEK_SET);
    read(sb->fd, in, sb->blksz);
}

void update_parent(struct superblock *sb, uint64_t parent_in_b, uint64_t parent_info_b, struct inode *parent_in, struct nodeinfo *parent_info)
{
    lseek(sb->fd, parent_in_b * sb->blksz, SEEK_SET);
    write(sb->fd, parent_in, sb->blksz);
    lseek(sb->fd, parent_info_b * sb->blksz, SEEK_SET);
    write(sb->fd, parent_info, sb->blksz);
}

void free_all_info(struct inode *in, struct inode *in2, struct inode *parent_in, struct nodeinfo *info, struct nodeinfo *info2, struct nodeinfo *parent_info)
{
    free(in);
    free(in2);
    free(parent_in);
    free(info);
    free(info2);
    free(parent_info);
}

int subfolders_to_vector(char *token, char *name, char files[MAX_SUBFOLDERS][MAX_NAME])
{
    int i = 0;

    token = strtok(name, "/");
    while (token != NULL)
    {
        strcpy(files[i], token);
        token = strtok(NULL, "/");
        i++;
    }
    return i;
}

int search_file_in_directory(struct superblock *sb, struct inode *in, struct inode *in2, struct nodeinfo *info, struct nodeinfo *info2, char *file_name)
{
    for (int k = 0; k < info->size; k++)
    {
        search_inode(sb, in, in2, info2, k);
        if (strcmp(info2->name, file_name) == 0)
            return 1; 
    }

    return 0; 
}


/* Escreve cnt bytes de buf no sistema de arquivos apontado por sb. Os dados serão
 * escritos num arquivo chamado fname. O parâmetro fname deve conter um caminho
 * absoluto. Retorna zero em caso de sucesso e um valor negativo em caso de erro; em
 * caso de erro, este será salvo em errno (p.ex., espaço em disco insuficiente). Se o
 * arquivo já existir, ele deverá ser sobrescrito por completo com os novos dados.
 */
int fs_write_file(struct superblock *sb, const char *fname, char *buf, size_t cnt)
{
    int i, j, k, current_allocated_blocks, found;
    int num_elements_in_path, num_new_blocks, new_file;
    uint64_t blocks[MAX_FILE_SIZE], parent_in_b, parent_info_b;

    char files[MAX_SUBFOLDERS][MAX_NAME];
    char *token;
    char *name;

    struct inode *in, *in2, *parent_in;
    struct nodeinfo *parent_info, *info, *info2;

    in = (struct inode *)malloc(sb->blksz);
    in2 = (struct inode *)malloc(sb->blksz);
    parent_in = (struct inode *)malloc(sb->blksz);
    info = (struct nodeinfo *)malloc(sb->blksz);
    info2 = (struct nodeinfo *)malloc(sb->blksz);
    parent_info = (struct nodeinfo *)malloc(sb->blksz);

    name = (char *)malloc(MAX_PATH_NAME * sizeof(char));
    strcpy(name, fname);

    num_elements_in_path = subfolders_to_vector(token, name, files);

    lseek(sb->fd, sb->blksz, SEEK_SET);
    read(sb->fd, info, sb->blksz);

    lseek(sb->fd, 2 * sb->blksz, SEEK_SET);
    read(sb->fd, in, sb->blksz);

    copy_inode(parent_in, in, info);
    copy_nodeinfo(parent_info, info);

    parent_info_b = 1;
    parent_in_b = 2;

    blocks[0] = 1;
    blocks[1] = 2;

    new_file = 0;
    for (j = 0; j < num_elements_in_path; j++)
    {
        while (1)
        {
            found = 0;

            for (k = 0; k < info->size; k++)
            {
                search_inode(sb, in, in2, info2, k);

                if (strcmp(info2->name, files[j]) == 0)
                {
                    found = 1;
                    break;
                }
            }

            if (found)
            {
                if (j == (num_elements_in_path - 1))
                {
                    info->size++;
                    blocks[0] = in2->meta;
                    blocks[1] = in->links[k];
                    new_file = 0;
                }
                else
                {
                    copy_inode(parent_in, in2, info2);
                    copy_nodeinfo(parent_info, info2);

                    parent_in_b = in->links[k];
                    parent_info_b = in2->meta;
                }
                break;
            }
            else
            {
                if (j == (num_elements_in_path - 1))
                {
                    // cria um novo arquivo

                    blocks[0] = fs_get_block(sb);
                    strcpy(info2->name, files[j]);
                    info2->size = sb->blksz - 20;

                    blocks[1] = fs_get_block(sb);
                    in2->mode = IMREG;
                    in2->parent = blocks[1];
                    in2->meta = blocks[0];
                    in2->next = 0;

                    new_file = 1;
                    break;
                }
                else if (in->next == 0)
                {
                    errno = ENOENT;
                    return -1;
                }
            }

            jump_to_next_inode(sb, in);
        }

        jump_to_next_dir(in, in2, info, info2);
    }
    // blocks[0] = nodeinfo
    // blocks[1] = primeiro inode

    if (new_file)
    {
        parent_in->links[parent_info->size] = blocks[1];
        parent_info->size++;

        num_new_blocks = ((float)cnt) / (sb->blksz - 20.0);
        if (num_new_blocks - (float)(cnt / (sb->blksz - 20)) >= EPS)
        {
            num_new_blocks++;
        }

        if (sb->freeblks < num_new_blocks)
        {
            errno = ENOSPC;
            return -1;
        }

        for (i = 0; i < num_new_blocks - 1; i++)
        {
            blocks[i + 2] = fs_get_block(sb);
        }
    }
    else
    {
        current_allocated_blocks = ((float)info->size) / (sb->blksz - 20.0);
        if (current_allocated_blocks - (float)(info->size / (sb->blksz - 20)) >= EPS)
            current_allocated_blocks++;

        num_new_blocks = ((float)cnt) / (sb->blksz - 20.0);
        if (num_new_blocks > (float)(cnt / (sb->blksz - 20)))
            num_new_blocks++;

        if (current_allocated_blocks < num_new_blocks || current_allocated_blocks == num_new_blocks)
        {
            i = 2;
            copy_inode(in2, in, info);

            while (in2->next != 0)
            {
                blocks[i] = in->next;
                lseek(sb->fd, blocks[i] * sb->blksz, SEEK_SET);
                read(sb->fd, in2, sb->blksz);
                i++;
            }

            for (i = current_allocated_blocks + 1; i <= num_new_blocks; i++)
                blocks[i] = fs_get_block(sb);
        }
        else
        {
            i = 2;
            copy_inode(in2, in, info);
            while (in2->next != 0)
            {
                blocks[i] = in->next;
                lseek(sb->fd, blocks[i] * sb->blksz, SEEK_SET);
                read(sb->fd, in2, sb->blksz);
                i++;
            }

            for (i = current_allocated_blocks; i > num_new_blocks; i--)
                if (fs_put_block(sb, blocks[i]) != 0)
                    return -1;
        }
    }

    update_parent(sb, parent_in_b, parent_info_b, parent_in, parent_info);

    lseek(sb->fd, blocks[0] * sb->blksz, SEEK_SET);
    write(sb->fd, info, sb->blksz);

    in->mode = IMREG;
    in->parent = blocks[1];
    in->meta = blocks[0];
    if (num_new_blocks == 1)
        in->next = 0;
    else
        in->next = blocks[2];

    lseek(sb->fd, blocks[1] * sb->blksz, SEEK_SET);
    write(sb->fd, in, sb->blksz);

    in->mode = IMCHILD;
    in->parent = blocks[1];

    for (i = 2; i <= num_new_blocks; i++)
    {
        in->meta = blocks[i - 1];
        if (i < num_new_blocks)
            in->next = blocks[i + 1];
        else
            in->next = 0;

        lseek(sb->fd, blocks[i] * sb->blksz, SEEK_SET);
        write(sb->fd, in, sb->blksz);
    }

    lseek(sb->fd, 0, SEEK_SET);
    if (write(sb->fd, sb, sb->blksz) < 0)
        return -1;

    free_all_info(in, in2, parent_in, info, info2, parent_info);

    return 0;
}

void read_root(struct superblock *sb, struct inode *in, struct nodeinfo *info)
{
    lseek(sb->fd, sb->root * sb->blksz, SEEK_SET);
    read(sb->fd, in, sb->blksz);

    lseek(sb->fd, sb->blksz, SEEK_SET);
    read(sb->fd, info, sb->blksz);
}

void jump_to_next_dir(struct inode *in, struct inode *in2, struct nodeinfo *info, struct nodeinfo *info2)
{
    copy_inode(in, in2, info2);
    copy_nodeinfo(info, info2);
}

void remove_deleted_directory(struct inode *parent_in, struct nodeinfo *parent_info, uint64_t blocks[MAX_FILE_SIZE])
{
    for (int i = 0; i < parent_info->size; i++)
    {
        if (parent_in->links[i] == blocks[1])
        {
            while (i < parent_info->size - 1)
            {
                parent_in->links[i] = parent_in->links[i + 1];
                i++;
            }
            break;
        }
    }
}

void write_superblock(struct superblock *sb)
{
    lseek(sb->fd, 0, SEEK_SET);
    write(sb->fd, sb, sb->blksz);
}


/*Remove o arquivo chamado fname do sistema de arquivos apontado por sb (os blocos
 * associados ao arquivo devem ser liberados). Retorna zero em caso de sucesso e um
 * valor negativo em caso de erro; em caso de erro, este será salvo em errno de acordo
 * com a função unlink em unistd.h (p.ex., arquivo não encontrado).
 */
int fs_unlink(struct superblock *sb, const char *fname)
{

    int i, j, k, found;
    int num_elements_in_path;
    uint64_t blocks[MAX_FILE_SIZE], parent_in_b, parent_info_b;
    char files[MAX_SUBFOLDERS][MAX_NAME];
    char *token, *name;

    struct inode *in = (struct inode *)malloc(sb->blksz);
    struct inode *in2 = (struct inode *)malloc(sb->blksz);
    struct inode *parent_in = (struct inode *)malloc(sb->blksz);
    struct nodeinfo *info = (struct nodeinfo *)malloc(sb->blksz);
    struct nodeinfo *info2 = (struct nodeinfo *)malloc(sb->blksz);
    struct nodeinfo *parent_info = (struct nodeinfo *)malloc(sb->blksz);

    name = (char *)malloc(MAX_PATH_NAME * sizeof(char));
    strcpy(name, fname);

    // separa as subpastas em vetores
    i = 0;
    token = strtok(name, "/");
    while (token != NULL)
    {
        strcpy(files[i], token);
        token = strtok(NULL, "/");
        i++;
    }
    num_elements_in_path = i;

    read_root(sb, in, info);

    parent_info_b = 1; // nodeinfo
    parent_in_b = 2;   // inode
    for (j = 0; j < num_elements_in_path; j++)
    {
        while (1)
        {
            char *file_name = files[j]; 
            found = search_file_in_directory(sb, in, in2, info, info2, file_name);

            if (found)
            {
                if (j == (num_elements_in_path - 1))
                {
                    blocks[0] = in2->meta;
                    blocks[1] = in->links[k];
                }
                else
                {
                    parent_info_b = in2->meta;
                    parent_in_b = in->links[k];
                }
                break;
            }
            else if (j == (num_elements_in_path - 1) || in->next == 0)
            {
                errno = ENOENT;
                return -1;
            }

            jump_to_next_inode(sb, in);
        }

        jump_to_next_dir(parent_in, in, parent_info, info);

        jump_to_next_dir(in, in2, info, info2);
    }

    fs_put_block(sb, blocks[0]);
    fs_put_block(sb, blocks[1]);

    remove_deleted_directory(parent_in, parent_info, blocks);

    parent_info->size--;
    update_parent(sb, parent_in_b, parent_info_b, parent_in, parent_info);

    write_superblock(sb);

    free_all_info(in, in2, parent_in, info, info2, parent_info);

    return 0;
}

/*Cria um diretório no caminho dpath. O caminho dpath deve ser absoluto (começar
 * com uma barra). Retorna zero em caso de sucesso e um valor negativo em caso de
 * erro; em caso de erro, este será salvo em errno de acordo com a função mkdir em
 * unistd.h (p.ex., diretório já existente, espaço em disco insuficiente). O caminho dpath
 * não deve conter espaços. A função fs_mkdir não precisa criar diretórios
 * recursivamente; ela cria apenas um diretório por vez. Para criar o diretório /x/y é
 * preciso antes criar o diretório /x; se o diretório /x não existir, retorne ENOENT
 */
int fs_mkdir(struct superblock *sb, const char *dname)
{
    int i, j, k, found;
    int num_elements_in_path;
    uint64_t blocks[MAX_FILE_SIZE];
    uint64_t block_info, block_inode;
    char files[MAX_SUBFOLDERS][MAX_NAME];
    char *token, *name;

    struct inode *in = (struct inode *)malloc(sb->blksz);
    struct inode *in2 = (struct inode *)malloc(sb->blksz);
    struct nodeinfo *info = (struct nodeinfo *)malloc(sb->blksz);
    struct nodeinfo *info2 = (struct nodeinfo *)malloc(sb->blksz);

    name = (char *)malloc(MAX_PATH_NAME * sizeof(char));
    strcpy(name, dname);

    num_elements_in_path = subfolders_to_vector(token, name, files);

    lseek(sb->fd, sb->blksz, SEEK_SET);
    read(sb->fd, info, sb->blksz);

    lseek(sb->fd, sb->root * sb->blksz, SEEK_SET);
    read(sb->fd, in, sb->blksz);

    blocks[0] = 1;
    blocks[1] = 2;

    for (j = 0; j < num_elements_in_path - 1; j++)
    {
        while (1)
        {
            char *file_name = files[j]; 
            found = search_file_in_directory(sb, in, in2, info, info2, file_name);

            if (found)
            {
                if (j == (num_elements_in_path - 2))
                {
                    blocks[0] = in2->meta;
                    blocks[1] = in->links[k];
                }
                break;
            }
            else if (j == (num_elements_in_path - 2) || in->next == 0)
            {
                errno = ENOENT;
                return -1;
            }

            jump_to_next_inode(sb, in);
        }

        jump_to_next_dir(in, in2, info, info2);
    }

    block_info = fs_get_block(sb);
    info2->size = 0; // The directory starts empty
    strcpy(info2->name, files[num_elements_in_path - 1]);

    lseek(sb->fd, block_info * sb->blksz, SEEK_SET);
    write(sb->fd, info2, sb->blksz);

    block_inode = fs_get_block(sb);
    in2->mode = IMDIR;
    in2->parent = block_inode;
    in2->meta = block_info;
    in2->next = 0;

    lseek(sb->fd, block_inode * sb->blksz, SEEK_SET);
    write(sb->fd, in2, sb->blksz);

    in->links[info->size] = block_inode;
    info->size++;

    lseek(sb->fd, blocks[0] * sb->blksz, SEEK_SET);
    write(sb->fd, info, sb->blksz);

    lseek(sb->fd, blocks[1] * sb->blksz, SEEK_SET);
    write(sb->fd, in, sb->blksz);

    write_superblock(sb);

    free(in);
    free(in2);
    free(info);
    free(info2);

    return 0;
}

/*Remove o diretório no caminho dname. O caminho dname deve ser absoluto (começar
 * com uma barra). Retorna zero em caso de sucesso e um valor negativo em caso de
 * erro; em caso de erro, este será salvo em errno de acordo com a função rmdir em
 * unistd.h (p.ex., ENOTEMPTY se o diretório não estiver vazio). O caminho dname não deve
 * conter espaços.
 */
int fs_rmdir(struct superblock *sb, const char *dname)
{

    int i, j, k, found;
    int num_elements_in_path;
    uint64_t blocks[MAX_FILE_SIZE], parent_in_b, parent_info_b;
    char files[MAX_SUBFOLDERS][MAX_NAME];
    char *token, *name;

    struct inode *in = (struct inode *)malloc(sb->blksz);
    struct inode *in2 = (struct inode *)malloc(sb->blksz);
    struct inode *parent_in = (struct inode *)malloc(sb->blksz);
    struct nodeinfo *info = (struct nodeinfo *)malloc(sb->blksz);
    struct nodeinfo *info2 = (struct nodeinfo *)malloc(sb->blksz);
    struct nodeinfo *parent_info = (struct nodeinfo *)malloc(sb->blksz);

    name = (char *)malloc(MAX_PATH_NAME * sizeof(char));
    strcpy(name, dname);

    num_elements_in_path = subfolders_to_vector(token, name, files);

    read_root(sb, in, info);

    blocks[0] = 1;
    blocks[1] = 2;

    parent_info_b = 1; // nodeinfo
    parent_in_b = 2;   // inode
    for (j = 0; j < num_elements_in_path; j++)
    {
        while (1)
        {
            char *file_name = files[j]; 
            found = search_file_in_directory(sb, in, in2, info, info2, file_name);

            if (found)
            {
                if (j == (num_elements_in_path - 1))
                {
                    blocks[0] = in2->meta;
                    blocks[1] = in->links[k];
                }
                else
                {
                    parent_info_b = in2->meta;
                    parent_in_b = in->links[k];
                }
                break;
            }
            else if (j == (num_elements_in_path - 1) || in->next == 0)
            {
                errno = ENOENT;
                return -1;
            }

            jump_to_next_inode(sb, in);
        }

        jump_to_next_dir(parent_in, in, parent_info, info);

        jump_to_next_dir(in, in2, info, info2);
    }

    if (info->size > 0)
    {
        errno = ENOTEMPTY;
        return -1;
    }

    // Free blocks of directory's nodeinfo and inode
    fs_put_block(sb, blocks[0]);
    fs_put_block(sb, blocks[1]);

    remove_deleted_directory(parent_in, parent_info, blocks);

    parent_info->size--;
    update_parent(sb, parent_in_b, parent_info_b, parent_in, parent_info);

    write_superblock(sb);

    free_all_info(in, in2, parent_in, info, info2, parent_info);

    return 0;
}

/*Retorna um string com o nome de todos os elementos (arquivos e diretórios) no
* diretório dname, os elementos devem estar separados por espaço. Os diretórios
* devem estar indicados com uma barra ao final do nome. A ordem dos arquivos no
* string retornado não é relevante. Por exemplo, se um diretório contém três
* elementos--um diretório d1 e dois arquivos f1 e f2--esta função deve retornar:

* “d1/ f1 f2”
*/
char *fs_list_dir(struct superblock *sb, const char *dname)
{
    int i, j, k, pos, found;
    int num_elements_in_path;
    char files[MAX_SUBFOLDERS][MAX_NAME];
    char *name, *elements, *token;

    struct inode *in = (struct inode *)malloc(sb->blksz);
    struct inode *in2 = (struct inode *)malloc(sb->blksz);
    struct nodeinfo *info = (struct nodeinfo *)malloc(sb->blksz);
    struct nodeinfo *info2 = (struct nodeinfo *)malloc(sb->blksz);

    name = (char *)malloc(MAX_PATH_NAME * sizeof(char));
    strcpy(name, dname);

    num_elements_in_path = subfolders_to_vector(token, name, files);

    read_root(sb, in, info);

    for (j = 0; j < num_elements_in_path; j++)
    {
        while (1)
        {
            char *file_name = files[j]; 
            found = search_file_in_directory(sb, in, in2, info, info2, file_name);

            if (found)
                break;

            else if (j == (num_elements_in_path - 1) || in->next == 0)
            {
                // O diretório não foi encontrado
                errno = ENOENT;
                elements = (char *)malloc(3 * sizeof(char));
                strcpy(elements, "-1");
                return elements;
            }
            jump_to_next_inode(sb, in);
        }

        jump_to_next_dir(in, in2, info, info2);
    }

    elements = (char *)malloc(MAX_PATH_NAME * sizeof(char));
    pos = 0;
    elements[0] = '\0';

    for (i = 0; i < info->size; i++)
    {
        search_inode(sb, in, in2, info2, i);

        strcpy((elements + pos), info2->name);
        pos += strlen(info2->name);

        if (in2->mode == IMDIR)
        {
            elements[pos] = '/';
            elements[pos + 1] = '\0';
            pos++;
        }

        if (i < info->size - 1)
        {
            elements[pos] = ' ';
            elements[pos + 1] = '\0';
            pos++;
        }
    }

    free(in);
    free(in2);
    free(info);
    free(info2);

    return elements;
}
