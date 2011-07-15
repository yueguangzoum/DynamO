/*    dynamo:- Event driven molecular dynamics simulator 
 *    http://www.marcusbannerman.co.uk/dynamo
 *    Copyright (C) 2009  Marcus N Campbell Bannerman <m.bannerman@gmail.com>
 *
 *    This program is free software: you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    version 3 as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <magnet/GL/context.hpp>

namespace magnet {
  namespace GL {

    //! \brief The available GL targets to which this Buffer may be
    //! bound.
    enum BufferBindTargets
      { ARRAY = GL_ARRAY_BUFFER,
	ELEMENT_ARRAY = GL_ELEMENT_ARRAY_BUFFER, 
	PIXEL_PACK_BUFFER = GL_PIXEL_PACK_BUFFER,
	PIXEL_UNPACK_BUFFER = GL_PIXEL_UNPACK_BUFFER
      };

    /*! \brief The possible host access patterns, if the host
     * accesses the data.
     */
    enum BufferUsage
      {
	STREAM_DRAW = GL_STREAM_DRAW, 
	STREAM_READ = GL_STREAM_READ, 
	STREAM_COPY = GL_STREAM_COPY, 
	STATIC_DRAW = GL_STATIC_DRAW, 
	STATIC_READ = GL_STATIC_READ, 
	STATIC_COPY = GL_STATIC_COPY, 
	DYNAMIC_DRAW = GL_DYNAMIC_DRAW, 
	DYNAMIC_READ = GL_DYNAMIC_READ, 
	DYNAMIC_COPY = GL_DYNAMIC_COPY
      };

    
    /*! \brief An OpenGL buffer object.
     *
     * This class is used to represent vertex/element/normal buffer
     * objects and provides some automatic memory handling for them.
     *
     * People might argue that by fixing the type of data stored in
     * the buffer (and so trying to make OpenGL type-safe) is a bad
     * idea when you want to interleave your vertex data. But please
     * consider splitting your data across multiple VBOs. It can
     * actually speed up your GL rendering and it makes the interface
     * so much nicer.
     *
     * \tparam T The type of the data in the buffer.
     */
    template <class T>
    class Buffer
    {
    public:
      inline Buffer(): 
	_size(0), 
	_context(NULL), 
	_cl_handle_init(false), 
	_cl_buffer_acquired(0) 
      {}

      inline ~Buffer() { deinit(); }

      /*! \brief Initialises the Buffer object with the passed data
       *
       *  This will create the underlying OpenGL buffer, and load it
       *  with the contents of data.
       *
       * \param data A vector containing the data to be loaded to the
       * Buffer.
       *
       * \param usage The expected host memory access pattern, used to
       * optimise performance.
       */
      inline void init(const std::vector<T>& data, BufferUsage usage = STATIC_DRAW)
      { init(data.size(), usage, &data[0]); }

      /*! \brief Initialises the Buffer object.
       *
       *  This will create the underlying OpenGL buffer, and load it
       *  with the contents of data passed.
       *
       * \param size The number of elements in the buffer.
       *
       * \param usage The expected host memory access pattern, used to
       * optimise performance.
       *
       * \param ptr A pointer to data to fill the buffer with. If it
       * is set to NULL, no data is loaded.
       */
      inline void init(size_t size, BufferUsage usage = STATIC_DRAW, const T* ptr = NULL)
      {
	if (size == 0)
	  M_throw() << "Cannot initialise GL::Buffer with 0 size!";

	deinit();
	_size = size;
	_context = &Context::getContext();
	glGenBuffersARB(1, &_buffer);	
	bind(ARRAY);
	glBufferData(ARRAY, _size * sizeof(T), ptr, usage);
      }
      
      //! \brief Attach the Buffer to a OpenGL target
      inline void bind(BufferBindTargets target) const 
      {
	glBindBufferARB(target, _buffer);	
      }

      //! \brief Map a buffer onto the host device memory space;
      inline T* map()
      {
	bind(ARRAY);
	return static_cast<T*>(glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE));
      }

      //! \brief Map a buffer onto the host device memory space
      inline const T* map() const
      {
	bind(ARRAY);
	return static_cast<const T*>(glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY));
      }

      //! \brief Releases a previous \ref map() call.
      inline void unmap() const
      {
	bind(ARRAY);
	glUnmapBuffer(GL_ARRAY_BUFFER);
      }

      /*! \brief Destroys any OpenGL resources associated with this
       * object.
       */
      inline void deinit() 
      {
#ifdef MAGNET_DEBUG
	if (_cl_buffer_acquired)
	  M_throw() << "Deinitialising a buffer which is acquired by the OpenCL system!";
#endif
	_cl_handle = ::cl::BufferGL();
	_cl_handle_init = false;
	if (_size)
	  glDeleteBuffersARB(1, &_buffer);
	_context = NULL;
	_size = 0;
      }
      
      //! \brief Test if the buffer has been allocated.
      inline bool empty() const { return !_size; }

      /*!\brief Returns the size in bytes of the allocated buffer, or
       * 0 if not allocated.
       */
      inline size_t byte_size() const { return _size * sizeof(T); }

      /*! \brief Returns the number of elements in the buffer.
       */
      inline size_t size() const { return _size; }

      /*! \brief Returns the underlying OpenGL handle for the
       * buffer */
      inline GLuint getGLObject() const { initTest(); return _buffer; }


      /*! \brief Returns the OpenGL context this buffer lives in.
       */
      inline Context& getContext() const { initTest(); return *_context; }

      /*! \brief Returns an OpenCL representation of this GL buffer.
       *
       * This increments an internal counter, and every \ref
       * acquireCLObject() must be matched by a call to \ref
       * releaseCLObject()! before the next GL render using this
       * buffer!
       */
      inline const ::cl::BufferGL& acquireCLObject()
      { 
	initTest();
	if (!_cl_handle_init)
	  {
	    _cl_handle_init = true;
	    _cl_handle = ::cl::BufferGL(_context->getCLContext(), 
					CL_MEM_READ_WRITE, 
					getGLObject());
	  }

	if ((_cl_buffer_acquired++) == 0)
	  {
	    std::vector<cl::Memory> buffers;
	    buffers.push_back(_cl_handle);
	    _context->getCLCommandQueue().enqueueAcquireGLObjects(&buffers);
	  }

	return _cl_handle; 
      }

      /*! \brief Releases the OpenCL representation of this GL buffer.
       *
       * This only releases the OpenCL representation if the \ref
       * releaseCLObject() calls match the number of \ref
       * acquireCLObject() calls.
       */
      inline void releaseCLObject()
      { 
	initTest(); 
#ifdef MAGNET_DEBUG
	if (!_cl_handle_init)
	  M_throw() << "Cannot release CL Object, its not initialised!";
	if (_cl_buffer_acquired == 0)
	  M_throw() << "Trying to release an already released object!";
#endif
	if (--_cl_buffer_acquired == 0)
	  {
	    std::vector<cl::Memory> buffers;
	    buffers.push_back(_cl_handle);
	    _context->getCLCommandQueue().enqueueReleaseGLObjects(&buffers);
	  }
      }

    protected:
      /*! \brief Guard function to test if the buffer is initialised.
       */
      inline void initTest() const { if (empty()) M_throw() << "Buffer is not initialized!"; }

      size_t _size;
      GLuint _buffer;
      Context* _context;
      ::cl::BufferGL _cl_handle;
      bool _cl_handle_init;
      size_t _cl_buffer_acquired;
    };
  }
}
