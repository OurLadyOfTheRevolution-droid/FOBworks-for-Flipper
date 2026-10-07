CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror

.PHONY: all test clean corpus secplus-live

all: test_link test_library_contract test_tx_session test_vehicle_decoder test_rollingpwn \
     test_saved_check test_classify test_keyvault test_hitag2 test_link_auth test_new_modules

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
		../protocol/flipper_scher_khan.c ../protocol/flipper_secplus.c

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

test: all
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
	./test_classify 40 --check

# Regenerate the committed corpus. Separate from `test` because it writes 1500 files into
# the tree; `test` only measures. Pass SETS=n to change the size.
corpus: test_classify
	./test_classify $(or $(SETS),500)

# Live Security+ honesty report against owned-hardware .sub captures.
secplus-live: sub_check
	./sub_check --report $(or $(LIVE),../../deliverables/secplus-live-captures)

clean:
	rm -f test_link test_library_contract test_tx_session test_vehicle_decoder \
	      test_rollingpwn test_saved_check test_classify test_keyvault \
	      test_hitag2 test_link_auth test_new_modules sub_check
