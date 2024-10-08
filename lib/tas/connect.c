/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <tas_ll_connect.h>
#include <tas_memif.h>

static void *map_region(const char *name, size_t len, int fd, off_t off);
static void *map_region_huge(const char *name, size_t len, int fd, off_t off)
    __attribute__((used));
static int flexnic_driver_connect_sing(struct flexnic_info **p_info, void **p_mem_start,
    int shmfd, int vmid);

static struct flexnic_info *info = NULL;

int flexnic_driver_connect_mult(struct flexnic_info **p_info, void **p_mem_start,
    int shmfd[])
{
  int vmid;
  int err_ret = -1;

  /* return error, if already connected */
  if (info != NULL) {
    fprintf(stderr, "flexnic_driver_connect: already connected\n");
    return -1;
  }

  for (vmid = 0; vmid < FLEXNIC_PL_VMST_NUM; vmid++)
  {
    err_ret = flexnic_driver_connect_sing(p_info, 
        &p_mem_start[vmid], shmfd[vmid], vmid);
    
    if (err_ret < 0)
    {
      fprintf(stderr, "flexnic_driver_connect_mult: driver connect for "
          "vm = %d failed.\n", vmid);
      return err_ret;
    }
  }

  return err_ret;
}

int flexnic_driver_connect(struct flexnic_info **p_info, void **p_mem_start,
    int shmfd)
{
  void *m;
  volatile struct flexnic_info *fi;
  int err_ret = -1;

  /* return error, if already connected */
  if (info != NULL) {
    fprintf(stderr, "flexnic_driver_connect: already connected\n");
    goto error_exit;
  }

  /* open and map flexnic info shm region */
  if ((m = map_region(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES, -1, 0)) == NULL) {
    perror("flexnic_driver_connect: map_region info failed");
    goto error_exit;
  }

  /* abort if not ready yet */
  fi = (volatile struct flexnic_info *) m;
  if ((fi->flags & FLEXNIC_FLAG_READY) != FLEXNIC_FLAG_READY) {
    err_ret = 1;
    goto error_unmap_info;
  }

  /* open and map dma shm region */
  if ((fi->flags & FLEXNIC_FLAG_HUGEPAGES) == FLEXNIC_FLAG_HUGEPAGES) {
    m = map_region_huge(FLEXNIC_NAME_DMA_MEM, fi->dma_mem_size, 
        shmfd, fi->dma_mem_off);
  } else {
    m = map_region(FLEXNIC_NAME_DMA_MEM, fi->dma_mem_size, 
        shmfd, fi->dma_mem_off);
  }
  if (m == NULL) {
    perror("flexnic_driver_connect: mapping dma memory failed");
    goto error_unmap_info;
  }

  *p_info = info = (struct flexnic_info *) fi;
  *p_mem_start = m;
  return 0;

error_unmap_info:
  munmap(m, FLEXNIC_INFO_BYTES);
error_exit:
  return err_ret;
}

int flexnic_driver_internal(void **int_mem_start)
{
  void *m;

  if (info == NULL) {
    fprintf(stderr, "flexnic_driver_internal: driver not connected\n");
    return -1;
  }

  /* open and map flexnic internal memory shm region */
  if ((info->flags & FLEXNIC_FLAG_HUGEPAGES) == FLEXNIC_FLAG_HUGEPAGES) {
    m = map_region_huge(FLEXNIC_NAME_INTERNAL_MEM, info->internal_mem_size,
        -1, 0);
  } else {
    m = map_region(FLEXNIC_NAME_INTERNAL_MEM, info->internal_mem_size,
        -1, 0);
  }
  if (m == NULL) {
    perror("flexnic_driver_internal: map_region failed");
    return -1;
  }

  *int_mem_start = m;
  return 0;
}


static int flexnic_driver_connect_sing(struct flexnic_info **p_info, void **p_mem_start,
    int shmfd, int vmid)
{
  void *m;
  volatile struct flexnic_info *fi;
  int err_ret = -1;

  /* open and map flexnic info shm region */
  if ((m = map_region(FLEXNIC_NAME_INFO, FLEXNIC_INFO_BYTES, -1, 0)) == NULL) {
    perror("flexnic_driver_connect: map_region info failed");
    goto error_exit;
  }

  /* abort if not ready yet */
  fi = (volatile struct flexnic_info *) m;
  if ((fi->flags & FLEXNIC_FLAG_READY) != FLEXNIC_FLAG_READY) {
    err_ret = 1;
    goto error_unmap_info;
  }

  /* open and map dma shm region */
  if ((fi->flags & FLEXNIC_FLAG_HUGEPAGES) == FLEXNIC_FLAG_HUGEPAGES) {
    char name[40];
    snprintf(name, sizeof(name), "%s/%s_vm%d", 
        FLEXNIC_HUGE_PREFIX, FLEXNIC_NAME_DMA_MEM, vmid);
    m = map_region_huge(name, fi->dma_mem_size, 
        shmfd, fi->dma_mem_off);
  } else {
    char name[30];
    snprintf(name, sizeof(name), "%s_vm%d", 
        FLEXNIC_NAME_DMA_MEM, vmid);
    m = map_region(name, fi->dma_mem_size, 
        shmfd, fi->dma_mem_off);
  }
  if (m == NULL) {
    perror("flexnic_driver_connect: mapping dma memory failed");
    goto error_unmap_info;
  }

  *p_info = info = (struct flexnic_info *) fi;
  *p_mem_start = m;
  return 0;

error_unmap_info:
  munmap(m, FLEXNIC_INFO_BYTES);
error_exit:
  return err_ret;
}

static void *map_region(const char *name, size_t len, int fd, off_t off)
{
  void *m;
  int fd_old = fd;

  if (fd == -1)
  {
    if ((fd = shm_open(name, O_RDWR, 0)) == -1) 
    {
      perror("map_region: shm_open memory failed");
      return NULL;
    }
  }

  m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
      fd, off);

  /* Close fd only if it wasn't passed in */
  if (fd_old == -1)
  {
    close(fd);

  }

  if (m == (void *) -1) 
  {
    perror("map_region: mmap failed");
    return NULL;
  }

  return m;
}

static void *map_region_huge(const char *name, size_t len, int fd, off_t off)
{
  void *m;
  char path[128];
  int fd_old = fd;

  snprintf(path, sizeof(path), "%s/%s", FLEXNIC_HUGE_PREFIX, name);

  if (fd == -1)
  {
    if ((fd = open(path, O_RDWR)) == -1) {
      perror("map_region_huge: shm_open memory failed");
      return NULL;
    }
  }

  m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, off);

  /* Close fd only if it wasn't passed in */
  if (fd_old == -1)
  {
    close(fd);
  }

  if (m == (void *) -1) {
    perror("map_region_huge: mmap failed");
    return NULL;
  }

  return m;
}
