   //#include <linux/config.h>  
    #include <linux/version.h>  
    #include <linux/init.h>  
    #include <linux/module.h>  
    #include <linux/fs.h>  
    #include <linux/cdev.h>  
    #include <linux/slab.h>  
    #include <linux/vmalloc.h>  
    #include <linux/mm.h>  
    #ifdef MODVERSIONS  
    #  include <linux/modversions.h>  
    #endif  
    #include <asm/io.h>  

    #define TRACE() printk("%s %d\r\n", __FUNCTION__, __LINE__)
      
    /* character device structures */  
    static dev_t mmap_dev;  
    static struct cdev mmap_cdev;  
      
    /* methods of the character device */  
    static int mmap_open(struct inode *inode, struct file *filp);  
    static int mmap_release(struct inode *inode, struct file *filp);  
    static int mmap_mmap(struct file *filp, struct vm_area_struct *vma);  
      
    /* the file operations, i.e. all character device methods */  
    static struct file_operations mmap_fops = {  
            .open = mmap_open,  
            .release = mmap_release,  
            .mmap = mmap_mmap,  
            .owner = THIS_MODULE,  
    };  
      
    // internal data  
    // length of the two memory areas  
    #define NPAGES 16  
    // pointer to the vmalloc'd area - alway page aligned  
    static int *vmalloc_area;  
    // pointer to the kmalloc'd area, rounded up to a page boundary  
    static int *kmalloc_area;  
    // original pointer for kmalloc'd area as returned by kmalloc  
    static void *kmalloc_ptr;  
      
    /* character device open method */  
    static int mmap_open(struct inode *inode, struct file *filp)  
    {  
            return 0;  
    }  
    /* character device last close method */  
    static int mmap_release(struct inode *inode, struct file *filp)  
    {  
            return 0;  
    }  
      
    // helper function, mmap's the kmalloc'd area which is physically contiguous  
    int mmap_kmem(struct file *filp, struct vm_area_struct *vma)  
    {  
            int ret;  
            long length = vma->vm_end - vma->vm_start;  
      
            TRACE();
            printk("length = %d\r\n, start = 0x%x end =0x%x", vma->vm_start, vma->vm_end); 
            /* check length - do not allow larger mappings than the number of 
               pages allocated */  
            if (length > NPAGES * PAGE_SIZE)  
                    return -EIO;  
      
            /* map the whole physically contiguous area in one piece */  
            if ((ret = remap_pfn_range(vma,  
                                       vma->vm_start,  
                                       virt_to_phys((void *)kmalloc_area) >> PAGE_SHIFT,  
                                       length,  
                                       vma->vm_page_prot)) < 0) {  
                    return ret;  
            }  
              
            return 0;  
    }  
    // helper function, mmap's the vmalloc'd area which is not physically contiguous  
    int mmap_vmem(struct file *filp, struct vm_area_struct *vma)  
    {  
            int ret;  
            long length = vma->vm_end - vma->vm_start;  
            unsigned long start = vma->vm_start;  
            char *vmalloc_area_ptr = (char *)vmalloc_area;  
            unsigned long pfn;  
            TRACE();
      
            /* check length - do not allow larger mappings than the number of 
               pages allocated */  
            if (length > NPAGES * PAGE_SIZE)  
                    return -EIO;  

            printk("length = %d start = 0x%0x PAGE_SIZE = %d\r\n", length, start, PAGE_SIZE);  
      
            /* loop over all pages, map it page individually */  
            while (length > 0) {  
                    TRACE();  
                    pfn = vmalloc_to_pfn(vmalloc_area_ptr);  
                    if ((ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE,  
                                               PAGE_SHARED)) < 0) {  
                            return ret;  
                    }  
                    start += PAGE_SIZE;  
                    vmalloc_area_ptr += PAGE_SIZE;  
                    length -= PAGE_SIZE;  
            }  
            return 0;  
    }  
      
    /* character device mmap method */  
    static int mmap_mmap(struct file *filp, struct vm_area_struct *vma)  
    {  
            /* at offset 0 we map the vmalloc'd area */  
            if (vma->vm_pgoff == 0) {  
                    return mmap_vmem(filp, vma);  
            }  
            /* at offset NPAGES we map the kmalloc'd area */  
            if (vma->vm_pgoff == NPAGES) {  
                    return mmap_kmem(filp, vma);  
            }  
            /* at any other offset we return an error */  
            return -EIO;  
    }  
      
    /* module initialization - called at module load time */  
    static int __init mmap_init_kernel(void)  
    {  
            int ret = 0;  
            int i;  
      
            /* allocate a memory area with kmalloc. Will be rounded up to a page boundary */  
            if ((kmalloc_ptr = kmalloc((NPAGES + 2) * PAGE_SIZE, GFP_KERNEL)) == NULL) {  
                    ret = -ENOMEM;  
                    goto out;  
            }  

            printk("kmalloc_ptr %p\n", kmalloc_ptr); 

            /* round it up to the page bondary */  
            kmalloc_area = (int *)((((unsigned long)kmalloc_ptr) + PAGE_SIZE - 1) & PAGE_MASK);  

            printk("kmalloc_area %p\n", kmalloc_area); 
      
            /* allocate a memory area with vmalloc. */  
            if ((vmalloc_area = (int *)vmalloc(NPAGES * PAGE_SIZE)) == NULL) {  
                    ret = -ENOMEM;  
                    goto out_kfree;  
            }  

            printk("vmalloc_area %p\n", vmalloc_area);
                       
            /* get the major number of the character device */  
            if ((ret = alloc_chrdev_region(&mmap_dev, 0, 1, "mmap")) < 0) {  
                    printk(KERN_ERR "could not allocate major number for mmap\n");  
                    goto out_vfree;  
            }  
      
            /* initialize the device structure and register the device with the kernel */  
            cdev_init(&mmap_cdev, &mmap_fops);  
            if ((ret = cdev_add(&mmap_cdev, mmap_dev, 1)) < 0) {  
                    printk(KERN_ERR "could not allocate chrdev for mmap\n");  
                    goto out_unalloc_region;  
            }  
      
            /* mark the pages reserved */  
            for (i = 0; i < NPAGES * PAGE_SIZE; i+= PAGE_SIZE) {  
                    SetPageReserved(vmalloc_to_page((void *)(((unsigned long)vmalloc_area) + i)));  
                    SetPageReserved(virt_to_page(((unsigned long)kmalloc_area) + i));  
            }  
      
            /* store a pattern in the memory - the test application will check for it */  
            for (i = 0; i < (NPAGES * PAGE_SIZE / sizeof(int)); i += 2) {  
                    vmalloc_area[i] = (0xeeff << 16) + i;  
                    vmalloc_area[i + 1] = (0xffee << 16) + i;  
                    kmalloc_area[i] = (0xaabb << 16) + i;  
                    kmalloc_area[i + 1] = (0xbbaa << 16) + i;  
            }  
    
            TRACE(); 
            printk("mmap kernel finish success\n");          
            
            return ret;  
              
      out_unalloc_region:  
            unregister_chrdev_region(mmap_dev, 1);  
      out_vfree:  
            vfree(vmalloc_area);  
      out_kfree:  
            kfree(kmalloc_ptr);  
      out:  
            return ret;  
    }  
      
    /* module unload */  
    static void __exit mmap_exit_kernel(void)  
    {  
            int i;  
     
            TRACE(); 
            /* remove the character deivce */  
            cdev_del(&mmap_cdev);  
            TRACE();
            unregister_chrdev_region(mmap_dev, 1);  
      
            TRACE();
            /* unreserve the pages */  
            for (i = 0; i < NPAGES * PAGE_SIZE; i+= PAGE_SIZE) {  
                    SetPageReserved(vmalloc_to_page((void *)(((unsigned long)vmalloc_area) + i)));  
                    SetPageReserved(virt_to_page(((unsigned long)kmalloc_area) + i));  
            }  
            TRACE();
            /* free the memory areas */  
            vfree(vmalloc_area);  
            kfree(kmalloc_ptr);  
    }  
      
    module_init(mmap_init_kernel);  
    module_exit(mmap_exit_kernel);  
    MODULE_DESCRIPTION("mmap demo driver");  
    MODULE_AUTHOR("Martin Frey <frey@scs.ch>");  
    MODULE_LICENSE("Dual BSD/GPL");  
