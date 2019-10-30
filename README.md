# MPI Sessions and MPI Process Sets

This is the README for MPI Sessions / MPI Process Sets prototype.

If you have any questions please contact:

Dai Yang <d.yang@tum.de> 


## Prerequisites

- An MPI distribution, e.g. [OpenMPI](https://openmpi.org)
- C compiler and MPICC compiler

## Building with Makefile

- Edit Makefile, edit CXXFLAGS and LDFLAGS according to the location of MPI sessions / MPI process sets
- Execute 
``` 
make 
```
- Our prototype creates the `libqmpi.a`, which is a static **PMPI** library intercepting **all** MPI functions in your application. This means that you have to load our library as a PMPI tool in order to use its functionality in your application. 
You application msut be linked with our QMPI library, which can be done with the following call. 
Note you do not need to link against the MPI library if you link to QMPI (**NO** `-lmpi`) !!!!!
```
-lqmpi
```

- Building the example application: 
```
cd sample
make
```

## Limitations
- Currently only support shared object (.so) based dynamic library tools
- Only supports linux system (and macOS), Windows is not supported. 


## Copyright

The QMPI interface is licensed under the BSD 3-clause license. 
You should consult the LICENSE file for further information. 

(C) 2019 Technical University of Munich, Chair of Computer Architecture and Parallel Systems. 


## Disclaimer
THIS APPLICATION IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
