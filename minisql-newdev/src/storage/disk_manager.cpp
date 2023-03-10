#include <stdexcept>
#include <sys/stat.h>

#include "glog/logging.h"
#include "page/bitmap_page.h"
#include "storage/disk_manager.h"

DiskManager::DiskManager(const std::string &db_file) : file_name_(db_file){
  std::scoped_lock<std::recursive_mutex> lock(db_io_latch_);
  db_io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
  // directory or file does not exist
  if (!db_io_.is_open()) {
    db_io_.clear();
    // create a new file
    db_io_.open(db_file, std::ios::binary | std::ios::trunc | std::ios::out);
    db_io_.close();
    // reopen with original mode
    db_io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
      throw std::exception();
    }
    //memset(meta_data_,0,PAGE_SIZE);
    Meta_Page_ = new DiskFileMetaPage;
    Meta_Page_->num_allocated_pages_=0;
    Meta_Page_->num_extents_=0;
  }
  else{
    ReadPhysicalPage(META_PAGE_ID, meta_data_);
    Meta_Page_ = new DiskFileMetaPage(meta_data_);
    uint32_t num = Meta_Page_->num_extents_;
    for (uint32_t i = 0; i < num; ++i) {
      char *bitmap_data = new char[DiskManager::BITMAP_SIZE];
      ReadPhysicalPage(i * (DiskManager::BITMAP_SIZE + 1) + 1, bitmap_data);
      memcpy(Bitmap_Page_[i].GetBitmap_Data(), bitmap_data, DiskManager::BITMAP_SIZE);
      delete [] bitmap_data;
    }
  }
}

void DiskManager::Close() {
  std::scoped_lock<std::recursive_mutex> lock(db_io_latch_);
  if (!closed) {
     db_io_.close();
    closed = true;
  }
}

void DiskManager::ReadPage(page_id_t logical_page_id, char *page_data) {
  ASSERT(logical_page_id >= 0, "Invalid page id.");
  ReadPhysicalPage(MapPageId(logical_page_id), page_data);
}

void DiskManager::WritePage(page_id_t logical_page_id, const char *page_data) {
  ASSERT(logical_page_id >= 0, "Invalid page id.");
  WritePhysicalPage(MapPageId(logical_page_id), page_data);
}

page_id_t DiskManager::AllocatePage(){
  if(Meta_Page_->GetAllocatedPages()==MAX_VALID_PAGE_ID){//no free page
    LOG(ERROR)<<"No free page";
    return INVALID_PAGE_ID;
  }
  //#pages of every extent
  size_t SIZE = DiskManager::BITMAP_SIZE;   
  //find the next_allocate_page
  uint32_t extent = Meta_Page_->GetExtentNums();
  uint32_t page_offset;
  //find the extent that has free page to allocate 
  // for(extent = 0; extent < PAGE_SIZE/4; extent++)
  //   if(Meta_Page_->GetExtentUsedPage(extent) >= DiskManager::BITMAP_SIZE)
  //     //this extent has no free page to allocate 
  //     continue;
  //   else
  //     //this extent has free page to allocate 
  //     break;
  if(Meta_Page_->GetExtentUsedPage(extent) >= DiskManager::BITMAP_SIZE){
    extent++;
    Meta_Page_->num_extents_++;
    //1.2 modify extent_used_page_
    Meta_Page_->extent_used_page_[extent] = 1;
  }
  else Meta_Page_->extent_used_page_[extent]++; // modify extent_used_page_
  Meta_Page_->num_allocated_pages_++;
  //allocate page
  if(!Bitmap_Page_[extent].AllocatePage(page_offset)) return INVALID_PAGE_ID;//fail
  //write bit_map
  //WritePhysicalPage(extent*( SIZE + 1 ) + 1, (char *)Bitmap_Page_[extent].GetBitmap_Data());
  //write meta_page
  memcpy(meta_data_, &(Meta_Page_->num_allocated_pages_), sizeof(uint32_t));
  memcpy(meta_data_ + sizeof(uint32_t), &(Meta_Page_->num_extents_), sizeof(uint32_t));
  memcpy(meta_data_ + 2 * sizeof(uint32_t), Meta_Page_->extent_used_page_, (PAGE_SIZE - 8) / 4 * sizeof(uint32_t));
  //WritePhysicalPage(META_PAGE_ID, meta_data_);
  page_id_t page_index = extent * SIZE + page_offset;
  return page_index;
}

void DiskManager::DeAllocatePage(page_id_t logical_page_id) {
  if(logical_page_id<0){
    LOG(ERROR) << "Invalid page id.";
    return;
  }
  size_t SIZE = DiskManager::BITMAP_SIZE;
  uint32_t extent = logical_page_id / SIZE; //Get the corresponding extent
  uint32_t page_offset = logical_page_id % SIZE;
  if(!Bitmap_Page_[extent].DeAllocatePage(page_offset)) return;//fail
  //write bit_map
  //WritePhysicalPage(extent*( SIZE + 1 ) + 1, (char *)Bitmap_Page_[extent].GetBitmap_Data());
  Meta_Page_->num_allocated_pages_--;
  Meta_Page_->extent_used_page_[extent]--;
  if(Meta_Page_->GetExtentUsedPage(extent)==0)
    Meta_Page_->num_extents_--;
  //write meta_page
  memcpy(meta_data_, &(Meta_Page_->num_allocated_pages_), sizeof(uint32_t));
  memcpy(meta_data_ + sizeof(uint32_t), &(Meta_Page_->num_extents_), sizeof(uint32_t));
  memcpy(meta_data_ + 2 * sizeof(uint32_t), Meta_Page_->extent_used_page_, (PAGE_SIZE - 8) / 4 * sizeof(uint32_t));
  //WritePhysicalPage(META_PAGE_ID, meta_data_);
}

bool DiskManager::IsPageFree(page_id_t logical_page_id) {
  size_t SIZE=DiskManager::BITMAP_SIZE;
  uint32_t extent = logical_page_id / SIZE; //Get the corresponding extent
  uint32_t page_offset = logical_page_id % SIZE;
  return Bitmap_Page_[extent].IsPageFree(page_offset);
}

page_id_t DiskManager::MapPageId(page_id_t logical_page_id) {
  size_t SIZE = DiskManager::BITMAP_SIZE;
  uint32_t extent = logical_page_id / SIZE; //check the extent the page is in
  uint32_t start = extent * ( SIZE + 1 ) + 1; //Get location of the corresponding bitmap
  page_id_t physical_page_id = start + logical_page_id % SIZE + 1;
  return physical_page_id;
}


int DiskManager::GetFileSize(const std::string &file_name) {
  struct stat stat_buf;
  int rc = stat(file_name.c_str(), &stat_buf);
  return rc == 0 ? stat_buf.st_size : -1;
}

void DiskManager::ReadPhysicalPage(page_id_t physical_page_id, char *page_data) {
  int offset = physical_page_id * PAGE_SIZE;
  // check if read beyond file length
  if (offset >= GetFileSize(file_name_)) {
#ifdef ENABLE_BPM_DEBUG
    LOG(INFO) << "Read less than a page" << std::endl;
#endif
    memset(page_data, 0, PAGE_SIZE);
  } else {
    // set read cursor to offset
    db_io_.seekp(offset);
    db_io_.read(page_data, PAGE_SIZE);
    // if file ends before reading PAGE_SIZE
    int read_count = db_io_.gcount();
    if (read_count < PAGE_SIZE) {
#ifdef ENABLE_BPM_DEBUG
      LOG(INFO) << "Read less than a page" << std::endl;
#endif
      memset(page_data + read_count, 0, PAGE_SIZE - read_count);
    }
  }
}

void DiskManager::WritePhysicalPage(page_id_t physical_page_id, const char *page_data) {
  size_t offset = static_cast<size_t>(physical_page_id) * PAGE_SIZE;
  // set write cursor to offset
  db_io_.seekp(offset);
  db_io_.write(page_data, PAGE_SIZE);
  // check for I/O error
  if (db_io_.bad()) {
    LOG(ERROR) << "I/O error while writing";
    return;
  }
  // needs to flush to keep disk file in sync
  db_io_.flush();
}