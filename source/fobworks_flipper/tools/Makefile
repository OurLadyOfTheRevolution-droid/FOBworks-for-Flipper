CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror

.PHONY: all test clean corpus secplus-live

all: test_fobscan_display test_link test_library_contract test_tx_session test_vehicle_decoder test_rollingpwn \
     test_saved_check test_classify test_keyvault test_hitag2 test_link_auth test_new_modules \
     test_bridge test_bridge_unconfigured test_plugin_lifetime

test_link: test_link.c ../link/flipper_link_proto.c
	$(CC) $(CFLAGS) -I.. -o $@ $^

test_library_contract: test_library_contract.c ../link/flipper_link_proto.c \
		../protocol/flipper_library_rules.c
	$(CC) $(CFLAGS) -DFLIPPER_LINK_HOST_TEST -I.. -o $@ $^

test_tx_session: test_tx_session.c ../protocol/flipper_tx_session.c
	$(CC) $(CFLAGS) -I.. -o $@ $^

OEM_DECODERS = ../protocol/flipper_oem_wire.c ../protocol/flipper_vehrke.c \
		../protocol/flipper_psa.c ../protocol/flipper_vag.c \
		../protocol/flipper_suzuki.c ../protocol/flipper_subaru.c \
		../protocol/flipper_kia.c ../protocol/flipper_gm.c \
		../protocol/flipper_ford.c ../protocol/flipper_chrysler.c \
		../protocol/flipper_scher_khan.c ../protocol/flipper_secplus.c \
		../protocol/flipper_fixed_force.c

test_vehicle_decoder: test_vehicle_decoder.c ../protocol/flipper_decoders.c \
		../protocol/flipper_keeloq.c ../protocol/flipper_scratch.c \
		../protocol/flipper_mazda.c ../protocol/flipper_toyota.c \
		../protocol/flipper_honda.c ../protocol/flipper_fiat.c \
		$(OEM_DECODERS)
	$(CC) $(CFLAGS) -I.. -o $@ $^

test_rollingpwn: test_rollingpwn.c ../protocol/flipper_rollingpwn.c
	$(CC) $(CFLAGS) -I.. -o $@ $^

test_classify: test_classify.c ../protocol/flipper_decoders.c \
		../protocol/flipper_keeloq.c ../protocol/flipper_scratch.c \
		../protocol/flipper_mazda.c ../protocol/flipper_toyota.c \
		../protocol/flipper_honda.c ../protocol/flipper_fiat.c \
		$(OEM_DECODERS)
	$(CC) $(CFLAGS) -I../protocol -o $@ $^

test_saved_check: test_saved_check.c ../protocol/flipper_saved_check.c \
		../protocol/flipper_decoders.c ../protocol/flipper_keeloq.c \
		../protocol/flipper_scratch.c ../protocol/flipper_mazda.c \
		../protocol/flipper_toyota.c ../protocol/flipper_honda.c \
		../protocol/flipper_fiat.c \
		$(OEM_DECODERS)
	$(CC) $(CFLAGS) -I.. -o $@ $^

test_keyvault: test_keyvault.c ../protocol/flipper_keyvault.c
	$(CC) $(CFLAGS) -I.. -o $@ $^

test_hitag2: test_hitag2.c ../protocol/flipper_hitag2.c
	$(CC) $(CFLAGS) -I.. -o $@ $^

test_link_auth: test_link_auth.c ../link/flipper_link_proto.c
	$(CC) $(CFLAGS) -I.. -o $@ $^

# Pure analytical modules added for FOBreport / rollback / fingerprint / track.
ANALYSIS_MODULES = ../protocol/flipper_fobreport.c \
		../protocol/flipper_grollback.c \
		../protocol/flipper_fobfreq.c \
		../protocol/flipper_fobtrack.c

test_new_modules: test_new_modules.c $(ANALYSIS_MODULES) \
		../protocol/flipper_decoders.c ../protocol/flipper_keeloq.c \
		../protocol/flipper_scratch.c ../protocol/flipper_mazda.c \
		../protocol/flipper_toyota.c ../protocol/flipper_honda.c \
		../protocol/flipper_fiat.c $(OEM_DECODERS)
	$(CC) $(CFLAGS) -I.. -o $@ $^

sub_check: sub_check.c ../protocol/flipper_decoders.c \
		../protocol/flipper_keeloq.c ../protocol/flipper_scratch.c \
		../protocol/flipper_mazda.c ../protocol/flipper_toyota.c \
		../protocol/flipper_honda.c ../protocol/flipper_fiat.c \
		$(OEM_DECODERS)
	$(CC) $(CFLAGS) -I.. -o $@ $^

test_bridge: test_bridge.cpp bridge_stubs/fake_bridge.h \
../../fobworks_wifi_bridge/fobworks_wifi_bridge.ino ../../fobworks_wifi_bridge/bridge_policy.h
	$(CXX) $(CXXFLAGS) -Ibridge_stubs -o $@ test_bridge.cpp

test_bridge_unconfigured: test_bridge.cpp bridge_stubs/fake_bridge.h \
../../fobworks_wifi_bridge/fobworks_wifi_bridge.ino ../../fobworks_wifi_bridge/bridge_policy.h
	$(CXX) $(CXXFLAGS) -DTEST_UNCONFIGURED -Ibridge_stubs -o $@ test_bridge.cpp

test_plugin_lifetime: test_plugin_lifetime.c ../protocol/flipper_plugin.c \
../plugins/fw_catalog.c ../protocol/flipper_vehicles.c
	$(CC) $(CFLAGS) -std=c11 -D_POSIX_C_SOURCE=200809L -Iplugin_stubs -I.. -pthread -o $@ $^

test_fobscan_display: test_fobscan_display.c ../scenes/flipper_fobscan_display.c
	$(CC) $(CFLAGS) -o $@ $^

test: all
	./test_fobscan_display
	./test_link
	./test_library_contract
	./test_tx_session
	./test_vehicle_decoder
	./test_rollingpwn
	./test_saved_check
	./test_keyvault
	./test_hitag2
	./test_link_auth
	./test_new_modules
	./test_bridge
	./test_bridge_unconfigured
	./test_plugin_lifetime
	./test_classify 40 --check

# Regenerate the committed corpus. Separate from `test` because it writes 1500 files into
# the tree; `test` only measures. Pass SETS=n to change the size.
corpus: test_classify
	./test_classify $(or $(SETS),500)

# Live Security+ honesty report against owned-hardware .sub captures.
# Empty drop folder is a soft success: print how to fill it, do not fail the run.
secplus-live: sub_check
	@dir="$(or $(LIVE),../../deliverables/secplus-live-captures)"; \
	mkdir -p "$$dir"; \
	n=$$(find "$$dir" -type f -name '*.sub' 2>/dev/null | wc -l | tr -d ' '); \
	if [ "$$n" = "0" ]; then \
	  echo "secplus-live: no .sub files in $$dir yet."; \
	  echo "Drop owned-hardware Security+ 1.0 / 2.0 captures there, then re-run."; \
	  echo "Synthetic SynGate_* corpus rows do not count for Auto promotion."; \
	  exit 0; \
	fi; \
	./sub_check --report "$$dir"

clean:
	rm -f test_fobscan_display test_link test_library_contract test_tx_session test_vehicle_decoder \
	      test_rollingpwn test_saved_check test_classify test_keyvault \
      test_hitag2 test_link_auth test_new_modules test_bridge test_bridge_unconfigured \
      test_plugin_lifetime sub_check
