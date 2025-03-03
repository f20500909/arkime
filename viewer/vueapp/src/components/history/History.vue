<template>

  <div class="history-page">
    <MolochCollapsible>
      <span class="fixed-header">
        <!-- search navbar -->
        <form class="history-search">
          <div class="p-1">
            <span class="fa fa-lg fa-question-circle text-theme-primary help-cursor mt-2 pull-right"
              title="Tip: use ? to replace a single character and * to replace zero or more characters in your query"
              v-b-tooltip.hover>
            </span>
            <button type="button"
              class="btn btn-sm btn-theme-tertiary pull-right ml-1 search-btn"
              @click="loadData">
              <span v-if="!shiftKeyHold">
                Search
              </span>
              <span v-else
                class="enter-icon">
                <span class="fa fa-long-arrow-left fa-lg">
                </span>
                <div class="enter-arm">
                </div>
              </span>
            </button>
            <div class="input-group input-group-sm">
              <div class="input-group-prepend">
                <span class="input-group-text input-group-text-fw">
                  <span v-if="!shiftKeyHold"
                    class="fa fa-search fa-fw">
                  </span>
                  <span v-else
                    class="query-shortcut">
                    Q
                  </span>
                </span>
              </div>
              <input type="text"
                @keyup.enter="loadData"
                @input="debounceSearch"
                class="form-control"
                v-model="searchTerm"
                v-focus-input="focusInput"
                @blur="onOffFocus"
                placeholder="Search for history in the table below"
              />
              <span class="input-group-append">
                <button type="button"
                  @click="clear"
                  :disabled="!searchTerm"
                  class="btn btn-outline-secondary btn-clear-input">
                  <span class="fa fa-close">
                  </span>
                </button>
              </span>
            </div>
            <div class="form-inline mt-1">
              <moloch-time
                :timezone="user.settings.timezone"
                @timeChange="loadData"
                :hide-bounding="true"
                :hide-interval="true">
              </moloch-time>
            </div>
          </div>
        </form> <!-- /search navbar -->

        <!-- paging navbar -->
        <form class="history-paging">
          <div class="form-inline">
            <moloch-paging v-if="history"
              class="mt-1 ml-1"
              :records-total="recordsTotal"
              :records-filtered="recordsFiltered"
              @changePaging="changePaging"
              length-default=100>
            </moloch-paging>
            <moloch-toast
              class="ml-2 mb-3 mt-1"
              :message="msg"
              :type="msgType"
              :done="messageDone">
            </moloch-toast>
          </div>
        </form> <!-- /paging navbar -->
      </span>
    </MolochCollapsible>

    <table class="table table-sm table-striped small">
      <thead>
        <tr>
          <th width="100px;">
            <button type="button"
              class="btn btn-xs btn-primary margined-bottom-sm"
              v-b-tooltip.hover
              title="Toggle column filters"
              @click="showColFilters = !showColFilters">
              <span class="fa fa-filter"></span>
            </button>
          </th>
          <th v-for="column of columns"
            :key="column.name"
            :class="`cursor-pointer ${column.classes}`"
            v-b-tooltip.hover
            :title="column.help"
            v-has-permission="column.permission"
            :style="{'width': `${column.width}%`}">
            <input type="text"
              v-if="column.filter && showColFilters"
              v-has-permission="column.permission"
              v-model="filters[column.sort]"
              :placeholder="`Filter by ${column.name}`"
              class="form-control form-control-sm input-filter"
              @keyup="debounceSearch"
              @click.stop
            />
            <div v-if="column.exists"
              class="mr-1 header-div">
              <input type="checkbox"
                class="checkbox"
                v-b-tooltip.hover
                :title="`Only show entries where the ${column.name} field exists`"
                @change="loadData"
                v-model="column.exists"
              />
            </div>
            <div class="header-div"
              @click="columnClick(column.sort)">
              <span v-if="column.sort !== undefined">
                <span v-show="sortField === column.sort && !desc" class="fa fa-sort-asc"></span>
                <span v-show="sortField === column.sort && desc" class="fa fa-sort-desc"></span>
                <span v-show="sortField !== column.sort" class="fa fa-sort"></span>
              </span>
              {{ column.name }}
            </div>
            <a v-if="filters[column.sort]"
              v-b-tooltip.hover
              :title="'The history is being filtered by ' + column.name + '. Click to display the filter.'"
              @click="showColFilters = true"
              class="cursor-pointer ml-1">
              <span class="fa fa-filter">
              </span>
            </a>
          </th>
        </tr>
      </thead>
      <tbody v-if="history">
        <!-- no results -->
        <tr v-if="!history.length">
          <td :colspan="colSpan"
            class="text-danger text-center">
            <span class="fa fa-warning">
            </span>&nbsp;
            <strong>
              No history or none that matches your search
            </strong>
          </td>
        </tr> <!-- /no results -->
        <template v-for="(item, index) of history">
          <!-- history item -->
          <tr :key="item.id">
            <td class="no-wrap">
              <toggle-btn class="mt-1"
                :opened="item.expanded"
                @toggle="toggleLogDetail(item)">
              </toggle-btn>
              <button
                type="button"
                role="button"
                title="Delete history"
                class="btn btn-xs btn-warning"
                v-has-role="{user:user,roles:'arkimeAdmin'}"
                v-has-permission="'removeEnabled'"
                @click="deleteLog(item, index)">
                <span class="fa fa-trash-o">
                </span>
              </button>
              <a class="btn btn-xs btn-info"
                v-if="item.uiPage"
                tooltip-placement="right"
                v-b-tooltip.hover
                :title="`Open this query on the ${item.uiPage} page`"
                @click="openPage(item)">
                <span class="fa fa-folder-open">
                </span>
              </a>
            </td>
            <td class="no-wrap">
              {{ item.timestamp * 1000 | timezoneDateString(user.settings.timezone) }}
            </td>
            <td class="no-wrap text-right">
              {{ item.range*1000 | readableTime }}
            </td>
            <td v-has-role="{user:user,roles:'arkimeAdmin'}"
              class="no-wrap">
              {{ item.userId }}
            </td>
            <td class="no-wrap text-right">
              {{ item.queryTime }}ms
            </td>
            <td class="no-wrap">
              {{ item.method }}
            </td>
            <td class="no-wrap"
              :title="item.api">
              {{ item.api }}
            </td>
            <td class="no-wrap">
              {{ item.expression }}
            </td>
            <td class="no-wrap">
              <span v-if="item.view">
                <strong>
                  {{ item.view.name }}
                </strong>
                {{ item.view.expression }}
              </span>
            </td>
          </tr> <!-- /history item -->
          <!-- history item info -->
          <tr :key="item.id+'-detail'"
            v-if="expandedLogs[item.id]">
            <td :colspan="colSpan">
              <dl class="dl-horizontal">
                <!-- forced expression -->
                <div v-has-role="{user:user,roles:'arkimeAdmin'}"
                  v-if="item.forcedExpression !== undefined"
                  class="mt-1">
                  <span>
                    <dt>Forced Expression</dt>
                    <dd class="break-word">{{ item.forcedExpression }}</dd>
                  </span>
                </div> <!-- /forced expression -->
                <!-- count info -->
                <div v-if="item.recordsReturned !== undefined"
                  class="mt-1">
                  <dt><h5>
                    Counts
                  </h5></dt>
                  <dd><h5>&nbsp;</h5></dd>
                  <span v-if="item.recordsReturned !== undefined">
                    <dt>Records Returned</dt>
                    <dd>{{ item.recordsReturned }}</dd>
                  </span>
                  <span v-if="item.recordsFiltered !== undefined">
                    <dt>Records Filtered</dt>
                    <dd>{{ item.recordsFiltered }}</dd>
                  </span>
                  <span v-if="item.recordsTotal !== undefined">
                    <dt>Records Total</dt>
                    <dd>{{ item.recordsTotal }}</dd>
                  </span>
                </div> <!-- /count info -->
                <!-- req body -->
                <div v-if="item.body"
                  class="mt-1">
                  <dt><h5>
                    Request Body
                  </h5></dt>
                  <dd><h5>&nbsp;</h5></dd>
                  <span v-for="(value, key) in item.body"
                    :key="key">
                    <dt>{{ key }}</dt>
                    <dd class="break-word">{{ value }}&nbsp;</dd>
                  </span>
                </div> <!-- /req body -->
                <!-- query params -->
                <div class="mt-2">
                  <template v-if="item.query">
                    <dt><h5>
                      Query parameters
                      <sup>
                        <span class="fa fa-info-circle text-theme-primary">
                        </span>
                      </sup>
                    </h5></dt>
                    <dd><h5>&nbsp;</h5></dd>
                    <span v-for="(value, key) in item.queryObj"
                      :key="key">
                      <dt>{{ key }}</dt>
                      <dd class="break-word">
                        {{ value }}&nbsp;
                        <span v-if="key === 'view' && item.view && item.view.expression">
                          ({{ item.view.expression }})
                        </span>
                      </dd>
                    </span>
                  </template>
                  <div class="mt-2">
                    <em>
                      <strong v-if="item.query">
                        <span class="fa fa-info-circle text-theme-primary">
                        </span>&nbsp;
                        Parsed from:
                      </strong>
                      <span style="word-break:break-all;">
                        {{ item.api }}{{ item.query ? '?' : '' }}{{ item.query }}
                      </span>
                    </em>
                  </div>
                </div> <!-- /query params -->
                <!-- es query -->
                <div v-has-role="{user:user,roles:'arkimeAdmin'}">
                  <div class="mt-3" v-if="item.esQueryIndices">
                    <h5>Elasticsearch Query Indices</h5>
                    <code class="mr-3 ml-3">{{ item.esQueryIndices }}</code>
                  </div>
                  <div class="mt-3" v-if="item.esQuery">
                    <h5>Elasticsearch Query</h5>
                    <pre class="mr-3 ml-3">{{ JSON.parse(item.esQuery) }}</pre>
                  </div>
                </div>
              </dl>
            </td>
          </tr> <!-- /history item info -->
        </template>
      </tbody>
    </table>

    <!-- loading overlay -->
    <moloch-loading
      v-if="loading && !error">
    </moloch-loading> <!-- /loading overlay -->

    <!-- error -->
    <moloch-error
      v-if="!loading && error"
      :message="error"
    /> <!-- /error -->

    <!-- hack to make vue watch expanded logs -->
    <div style="display:none;">
      {{ expandedLogs }}
    </div>

  </div>

</template>

<script>
import qs from 'qs';
import MolochPaging from '../utils/Pagination';
import MolochLoading from '../utils/Loading';
import MolochError from '../utils/Error';
import ToggleBtn from '../../../../../common/vueapp/ToggleBtn';
import MolochTime from '../search/Time';
import FocusInput from '../utils/FocusInput';
import MolochToast from '../utils/Toast';
import MolochCollapsible from '../utils/CollapsibleWrapper';
import HistoryService from './HistoryService';

let searchInputTimeout; // timeout to debounce the search input

export default {
  name: 'ArkimeHistory',
  components: {
    MolochPaging,
    MolochLoading,
    MolochError,
    MolochTime,
    ToggleBtn,
    MolochToast,
    MolochCollapsible
  },
  directives: { FocusInput },
  data: function () {
    return {
      error: '',
      loading: true,
      history: {},
      recordsTotal: 0,
      recordsFiltered: 0,
      expandedLogs: { change: false },
      showColFilters: false,
      colSpan: 8,
      filters: {},
      sortField: 'timestamp',
      searchTerm: '',
      desc: true,
      msg: '',
      msgType: undefined,
      columns: [
        { name: 'Time', sort: 'timestamp', nowrap: true, width: 13, help: 'The time of the request' },
        { name: 'Time Range', sort: 'range', nowrap: true, width: 11, classes: 'text-right', help: 'The time range of the request' },
        { name: 'User ID', sort: 'userId', nowrap: true, width: 8, filter: true, role: 'arkimeAdmin', help: 'The id of the user that initiated the request' },
        { name: 'Query Time', sort: 'queryTime', nowrap: true, width: 8, classes: 'text-right', help: 'Execution time in MS' },
        { name: 'Method', sort: 'method', nowrap: true, width: 5, help: 'The HTTP request method' },
        { name: 'API', sort: 'api', nowrap: true, width: 13, filter: true, help: 'The API endpoint of the request' },
        { name: 'Expression', sort: 'expression', nowrap: true, width: 27, exists: false, help: 'The query expression issued with the request' },
        { name: 'View', sort: 'view.name', nowrap: true, width: 20, exists: false, help: 'The view expression applied to the request' }
      ]
    };
  },
  computed: {
    query: function () {
      return { // query defaults
        length: parseInt(this.$route.query.length) || 50,
        start: 0,
        date: this.$store.state.timeRange,
        startTime: this.$store.state.time.startTime,
        stopTime: this.$store.state.time.stopTime
      };
    },
    user: function () {
      return this.$store.state.user;
    },
    focusInput: {
      get: function () {
        return this.$store.state.focusSearch;
      },
      set: function (newValue) {
        this.$store.commit('setFocusSearch', newValue);
      }
    },
    shiftKeyHold: function () {
      return this.$store.state.shiftKeyHold;
    },
    issueSearch: function () {
      return this.$store.state.issueSearch;
    }
  },
  watch: {
    issueSearch: function (newVal, oldVal) {
      if (newVal) { this.loadData(); }
    }
  },
  created: function () {
    // if the user is an admin, show them all the columns
    if (this.user.roles.includes('arkimeAdmin')) { this.colSpan = 9; }
    // query for the user requested or the current user
    this.filters.userId = this.$route.query.userId || this.user.userId;

    setTimeout(() => {
      // wait query to be computed
      this.loadData();
    });
  },
  methods: {
    /* exposed page functions ------------------------------------ */
    debounceSearch: function () {
      if (searchInputTimeout) { clearTimeout(searchInputTimeout); }
      // debounce the input so it only issues a request after keyups cease for 400ms
      searchInputTimeout = setTimeout(() => {
        searchInputTimeout = null;
        this.loadData();
      }, 400);
    },
    clear () {
      this.searchTerm = undefined;
      this.loadData();
    },
    onOffFocus: function () {
      this.focusInput = false;
    },
    columnClick: function (colName) {
      this.sortField = colName;
      this.desc = !this.desc;
      this.loadData();
    },
    toggleLogDetail: function (log) {
      log.expanded = !log.expanded;

      this.expandedLogs.change = !this.expandedLogs.change;
      this.expandedLogs[log.id] = !this.expandedLogs[log.id];

      if (log.query && log.expanded && !log.queryObj) {
        log.queryObj = {};
        // match only single & (&& can be in the search expression)
        const a = (log.query[0] === '?' ? log.query.substr(1) : log.query).split(/&(?![&& ])/g);
        for (let i = 0, len = a.length; i < len; i++) {
          const b = a[i].split(/=(.+)/); // splits on first '=';
          let value = b[1] || '';
          if (b[0] === 'expression') { value = value.replace(/\+/g, ' '); }
          log.queryObj[decodeURIComponent(b[0])] = decodeURIComponent(value);
        }
      }
    },
    deleteLog: function (log, index) {
      HistoryService.delete(log.id, log.index)
        .then((response) => {
          this.history.splice(index, 1);
          this.msg = response.data.text || 'Successfully deleted history item';
          this.msgType = 'success';
        })
        .catch((error) => {
          this.msg = error.text || 'Error deleting history item';
          this.msgType = 'danger';
        });
    },
    /* opens the page that this history item represents */
    openPage (item) {
      let query = item.query;
      if (item.expression && item.query.includes(item.expression)) {
        // need to remove expression because browsers can't handle the &&
        const remove = `&expression=${item.expression}`;
        const splitQuery = item.query.split(remove);
        query = splitQuery.join('');
      }

      this.$router.push({
        query: {
          ...qs.parse(query),
          expression: item.expression
        },
        path: item.uiPage
      });
    },
    /* remove the message when user is done with it or duration ends */
    messageDone: function () {
      this.msg = '';
      this.msgType = undefined;
    },
    /* helper functions ------------------------------------------ */
    loadData: function () {
      this.loading = true;

      const exists = [];
      for (let i = 0, len = this.columns.length; i < len; ++i) {
        const col = this.columns[i];
        if (col.exists) { exists.push(col.sort); }
      }
      if (exists.length) {
        this.query.exists = exists.join();
      } else {
        this.query.exists = undefined;
      }

      if (this.filters && Object.keys(this.filters).length) {
        for (const key in this.filters) {
          this.query[key] = this.filters[key];
        }
      }

      this.query.desc = this.desc;
      this.query.sortField = this.sortField;
      this.query.searchTerm = this.searchTerm;

      HistoryService.get(this.query)
        .then((response) => {
          this.error = '';
          this.loading = false;
          this.history = response.data.data;
          this.recordsTotal = response.data.recordsTotal;
          this.recordsFiltered = response.data.recordsFiltered;
        })
        .catch((error) => {
          this.loading = false;
          this.error = error.text || error;
        });
    },
    /* event functions ------------------------------------------- */
    changePaging: function (pagingValues) {
      this.query.length = pagingValues.length;
      this.query.start = pagingValues.start;

      this.loadData();
    }
  }
};
</script>

<style scoped>
/* navbar styles ------------------- */
.history-page form.history-search {
  z-index: 5;
  border: none;
  background-color: var(--color-secondary-lightest);
  position: relative;
  -webkit-box-shadow: 0 0 16px -2px black;
     -moz-box-shadow: 0 0 16px -2px black;
          box-shadow: 0 0 16px -2px black;
}

.history-page form .time-range-control {
  -webkit-appearance: none;
}

/* navbar with pagination */
.history-page form.history-paging {
  z-index: 4;
  height: 40px;
}

.input-group {
  flex-wrap: none;
  width: auto;
}

/* table styles -------------------- */
.history-page table {
  margin-top: 10px;
  table-layout: fixed;
}
.history-page table tbody tr td.no-wrap {
  white-space: nowrap;
  text-overflow: ellipsis;
  overflow: hidden;
}
/* super tiny input boxes for column filters */
.history-page table thead tr th input.input-filter {
  height: 24px;
  padding: 2px 5px;
  font-size: 12px;
  margin-bottom: 2px;
  margin-top: 2px;
}
.history-page table thead tr th div.header-div {
  display: inline-block;
}
.history-page table thead tr th div.header-div input.checkbox {
  margin-bottom: -2px;
}
/* shrink the toggle button */
.history-page table tbody tr td a.btn-toggle {
  padding: 1px 5px;
  font-size: 12px;
  line-height: 1.5;
  border-radius: 3px;
  margin-top: -2px !important;
}
</style>
