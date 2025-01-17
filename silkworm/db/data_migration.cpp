/*
   Copyright 2024 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "data_migration.hpp"

namespace silkworm::db {

void DataMigration::run() {
    cleanup();
    auto command = next_command();
    if (!command) return;
    auto result = migrate(std::move(command));
    index(result);
    commit(result);
    cleanup();
}

}  // namespace silkworm::db
