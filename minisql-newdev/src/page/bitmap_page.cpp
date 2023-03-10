#include "page/bitmap_page.h"

//set the corresponding bit to allocate or deallocate the page
void Set_Bit_map(uint32_t page_offset, unsigned char bytes[], bool bit){
  uint32_t byte_index=page_offset/8;
  uint8_t bit_index=page_offset%8; 
  unsigned char map_byte=bytes[byte_index];
  std::bitset<8> bitmap((unsigned long long)map_byte);
  if(bit)
    bitmap[bit_index]=1;
  else
    bitmap[bit_index]=0;
  bytes[byte_index]=(unsigned char)bitmap.to_ulong();
}


template<size_t PageSize>
bool BitmapPage<PageSize>::AllocatePage(uint32_t &page_offset) {
  //page_offset needs to be modified to the index of that allocated page
  if(this->page_allocated_==GetMaxSupportedSize())
    return false;
  else{
    this->page_allocated_++;
    page_offset=free_page_list_.front();
    free_page_list_.pop_front();
    //set bitmap
    Set_Bit_map(page_offset, this->bytes, true);
    return true;
  }
}

template<size_t PageSize>
bool BitmapPage<PageSize>::DeAllocatePage(uint32_t page_offset) {
  if(IsPageFree(page_offset))
    return false;
  else{
    this->page_allocated_--;
    free_page_list_.push_back(page_offset);
    Set_Bit_map(page_offset, this->bytes, false);
    return true;
  }
}

template<size_t PageSize>
bool BitmapPage<PageSize>::IsPageFree(uint32_t page_offset) const {
  uint32_t byte_index=page_offset/8;
  uint8_t bit_index=page_offset%8; 
  return IsPageFreeLow(byte_index, bit_index);
}

template<size_t PageSize>
bool BitmapPage<PageSize>::IsPageFreeLow(uint32_t byte_index, uint8_t bit_index) const {
  unsigned char map_byte=bytes[byte_index];//get which byte the page is in
  std::bitset<8> bitmap((unsigned long long)map_byte);
  return (bitmap[bit_index])?false:true;
}

//Template class instantiation
template 
class BitmapPage<64>;

template
class BitmapPage<128>;

template
class BitmapPage<256>;

template
class BitmapPage<512>;

template
class BitmapPage<1024>;

template
class BitmapPage<2048>;

template
class BitmapPage<4096>;