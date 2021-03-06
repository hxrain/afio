/* Examples of AFIO use
(C) 2018 Niall Douglas <http://www.nedproductions.biz/> (2 commits)
File Created: Aug 2018


Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License in the accompanying file
Licence.txt or at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


Distributed under the Boost Software License, Version 1.0.
    (See accompanying file Licence.txt or copy at
          http://www.boost.org/LICENSE_1_0.txt)
*/

#include "../include/afio.hpp"

#include <future>
#include <vector>

// clang-format off
#ifdef _MSC_VER
#pragma warning(disable: 4706)  // assignment within conditional
#endif

void read_entire_file1()
{
  //! [file_entire_file1]
  namespace afio = AFIO_V2_NAMESPACE;

  // Open the file for read
  afio::file_handle fh = afio::file(  //
    {},       // path_handle to base directory
    "foo"     // path_view to path fragment relative to base directory
              // default mode is read only
              // default creation is open existing
              // default caching is all
              // default flags is none
  ).value();  // If failed, throw a filesystem_error exception

  // Make a vector sized the current maximum extent of the file
  std::vector<afio::byte> buffer(fh.maximum_extent().value());

  // Synchronous scatter read from file
  afio::file_handle::buffers_type filled = afio::read(
    fh,                                 // handle to read from
    0,                                  // offset
    {{ buffer.data(), buffer.size() }}  // Single scatter buffer of the vector 
                                        // default deadline is infinite
  ).value();                            // If failed, throw a filesystem_error exception

  // In case of racy truncation of file by third party to new length, adjust buffer to
  // bytes actually read
  buffer.resize(filled[0].len);
  //! [file_entire_file1]
}

void read_entire_file2()
{
  //! [file_entire_file2]
  namespace afio = AFIO_V2_NAMESPACE;

  // Create an i/o service to complete the async file i/o
  afio::io_service service;

  // Open the file for read
  afio::async_file_handle fh = afio::async_file(  //
    service,  // The i/o service to complete i/o to
    {},       // path_handle to base directory
    "foo"     // path_view to path fragment relative to base directory
              // default mode is read only
              // default creation is open existing
              // default caching is all
              // default flags is none
  ).value();  // If failed, throw a filesystem_error exception

  // Get the valid extents of the file.
  const std::vector<
    std::pair<afio::file_handle::extent_type, afio::file_handle::extent_type>
  > valid_extents = fh.extents().value();

  // Schedule asynchronous reads for every valid extent
  std::vector<std::pair<std::vector<afio::byte>, afio::async_file_handle::io_state_ptr>> buffers(valid_extents.size());
  for (size_t n = 0; n < valid_extents.size(); n++)
  {
    // Set up the scatter buffer
    buffers[n].first.resize(valid_extents[n].second);
    for(;;)
    {
      afio::async_file_handle::buffer_type scatter_req{ buffers[n].first.data(), buffers[n].first.size() };  // buffer to fill
      auto ret = afio::async_read( //
        fh,                                 // handle to read from
        { { scatter_req } , 0 },            // The scatter request buffers
        [](                                                                            // The completion handler
          afio::async_file_handle *,                                                   // The parent handle
          afio::async_file_handle::io_result<afio::async_file_handle::buffers_type> &  // Result of the i/o
          ) { /* do nothing */ }
        // default deadline is infinite
      );
      // Was the operation successful?
      if (ret)
      {
        // Retain the handle to the outstanding i/o
        buffers[n].second = std::move(ret).value();
        break;
      }
      if (ret.error() == std::errc::resource_unavailable_try_again)
      {
        // Many async file i/o implementations have limited total system concurrency
        std::this_thread::yield();
        continue;
      }
      // Otherwise, throw a filesystem_error exception
      ret.value();
    }
  }

  // Pump i/o completion until no work remains
  while (service.run().value())
  {
    // run() returns per completion handler dispatched if work remains
    // It blocks until some i/o completes (there is a polling and deadline based overload)
    // If no work remains, it returns false
  }

  // Gather the completions of all i/o scheduled for success and errors
  for (auto &i : buffers)
  {
    // Did the read succeed?
    if (i.second->result.read)
    {
      // Then adjust the buffer size to that actually read
      i.first.resize(i.second->result.read.value().size());
    }
    else
    {
      // Throw the cause of failure as an exception
      i.second->result.read.value();
    }
  }
  //! [file_entire_file2]
}

void scatter_write()
{
  //! [scatter_write]
  namespace afio = AFIO_V2_NAMESPACE;

  // Open the file for write, creating if needed, don't cache reads nor writes
  afio::file_handle fh = afio::file(  //
    {},                                         // path_handle to base directory
    "hello",                                    // path_view to path fragment relative to base directory
    afio::file_handle::mode::write,             // write access please
    afio::file_handle::creation::if_needed,     // create new file if needed
    afio::file_handle::caching::only_metadata   // cache neither reads nor writes of data on this handle
                                                // default flags is none
  ).value();                                    // If failed, throw a filesystem_error exception

  // Empty file
  fh.truncate(0).value();

  // Perform gather write
  const char a[] = "hel";
  const char b[] = "l";
  const char c[] = "lo w";
  const char d[] = "orld";

  fh.write(0,                // offset
    {                        // gather list, buffers use std::byte
      { reinterpret_cast<const afio::byte *>(a), sizeof(a) - 1 },
      { reinterpret_cast<const afio::byte *>(b), sizeof(b) - 1 },
      { reinterpret_cast<const afio::byte *>(c), sizeof(c) - 1 },
      { reinterpret_cast<const afio::byte *>(d), sizeof(d) - 1 },
    }
                            // default deadline is infinite
  ).value();                // If failed, throw a filesystem_error exception

  // Explicitly close the file rather than letting the destructor do it
  fh.close().value();
  //! [scatter_write]
}

void map_file()
{
  //! [map_file]
  namespace afio = AFIO_V2_NAMESPACE;

  // Open the file for read
  afio::file_handle rfh = afio::file(  //
    {},       // path_handle to base directory
    "foo"     // path_view to path fragment relative to base directory
              // default mode is read only
              // default creation is open existing
              // default caching is all
              // default flags is none
  ).value();  // If failed, throw a filesystem_error exception

  // Open the same file for atomic append
  afio::file_handle afh = afio::file(  //
    {},                               // path_handle to base directory
    "foo",                            // path_view to path fragment relative to base directory
    afio::file_handle::mode::append   // open for atomic append
                                      // default creation is open existing
                                      // default caching is all
                                      // default flags is none
  ).value();                          // If failed, throw a filesystem_error exception

  // Create a section for the file of exactly the current length of the file
  afio::section_handle sh = afio::section(rfh).value();

  // Map the end of the file into memory with a 1Mb address reservation
  afio::map_handle mh = afio::map(sh, 1024 * 1024, sh.length().value() & ~4095).value();

  // Append stuff to append only handle
  afio::write(afh,
    0,                                                      // offset is ignored for atomic append only handles
    {{ reinterpret_cast<const afio::byte *>("hello"), 6 }}  // single gather buffer
                                                            // default deadline is infinite
  ).value();

  // Poke map to update itself into its reservation if necessary to match its backing
  // file, bringing the just appended text into the map. A no-op on many platforms.
  size_t length = mh.update_map().value();

  // Find my appended text
  for (char *p = reinterpret_cast<char *>(mh.address()); (p = (char *) memchr(p, 'h', reinterpret_cast<char *>(mh.address()) + length - p)); p++)
  {
    if (strcmp(p, "hello"))
    {
      std::cout << "Happy days!" << std::endl;
    }
  }
  //! [map_file]
}

void mapped_file()
{
  //! [mapped_file]
  namespace afio = AFIO_V2_NAMESPACE;

  // Open the mapped file for read
  afio::mapped_file_handle mh = afio::mapped_file(  //
    {},       // path_handle to base directory
    "foo"     // path_view to path fragment relative to base directory
              // default mode is read only
              // default creation is open existing
              // default caching is all
              // default flags is none
  ).value();  // If failed, throw a filesystem_error exception

  auto length = mh.maximum_extent().value();

  // Find my text
  for (char *p = reinterpret_cast<char *>(mh.address()); (p = (char *)memchr(p, 'h', reinterpret_cast<char *>(mh.address()) + length - p)); p++)
  {
    if (strcmp(p, "hello"))
    {
      std::cout << "Happy days!" << std::endl;
    }
  }
  //! [mapped_file]
}

void sparse_array()
{
  //! [sparse_array]
  namespace afio = AFIO_V2_NAMESPACE;

  // Make me a 1 trillion element sparsely allocated integer array!
  afio::mapped_file_handle mfh = afio::mapped_temp_inode().value();

  // On an extents based filing system, doesn't actually allocate any physical
  // storage but does map approximately 4Tb of all bits zero data into memory
  (void) mfh.truncate(1000000000000ULL * sizeof(int));

  // Create a typed view of the one trillion integers
  afio::algorithm::mapped_span<int> one_trillion_int_array(mfh);

  // Write and read as you see fit, if you exceed physical RAM it'll be paged out
  one_trillion_int_array[0] = 5;
  one_trillion_int_array[999999999999ULL] = 6;
  //! [sparse_array]
}

#ifdef __cpp_coroutines
std::future<void> coroutine_write()
{
  //! [coroutine_write]
  namespace afio = AFIO_V2_NAMESPACE;

  // Create an asynchronous file handle
  afio::io_service service;
  afio::async_file_handle fh =
    afio::async_file(service, {}, "testfile.txt",
      afio::async_file_handle::mode::write,
      afio::async_file_handle::creation::if_needed).value();

  // Resize it to 1024 bytes
  truncate(fh, 1024).value();

  // Begin to asynchronously write "hello world" into the file at offset 0,
  // suspending execution of this coroutine until completion and then resuming
  // execution. Requires the Coroutines TS.
  alignas(4096) char buffer[] = "hello world";
  co_await co_write(fh, 0, { { reinterpret_cast<afio::byte *>(buffer), sizeof(buffer) } }).value();
  //! [coroutine_write]
}
#endif

int main()
{
  return 0;
}