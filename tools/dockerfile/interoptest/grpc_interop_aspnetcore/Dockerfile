# Copyright 2019 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

FROM mcr.microsoft.com/dotnet/aspnet:8.0

# needed by get-dotnet.sh script
RUN apt-get update && apt-get install -y curl git jq && apt-get clean

RUN git config --global --add safe.directory '*'
RUN git config --global protocol.file.allow always

# Define the default command.
CMD ["bash"]
