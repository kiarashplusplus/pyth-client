# Understanding publishing slots

test_publish forwards what it thinks is the current slot (its publishing slot) along with the price when it publishes a new quote to solana.  test_publish receives notification of all price updates on the block-chain and can match what it sent with what it received by joining on the slot number.  This can be done because aggregate price update includes all the component prices that went into computing the aggregate.  Each component price is accompanied by its publisher key and publishing slot.

Here is an excerpt of a log take from a run of the test_publish.cpp example program against mainnet-beta. It logs everything it sends and everything it receives.

The publishing slots of six consecutive price submissions have been annotated with the labels A, B, C, D, E and F or slots 79018079, 79018084, 79018085, 79018086, 79018087, 79018092.

Please note that prices for slots 79018080 thru 79018083 and 79018088 thru 79018091 were not submitted. This is because solana does not always publish consective slots and gaps can occur.  Solana can even publish slots out-of-order, but the API ignores these and is guaranteed to issue callbacks for slots that are strictly increasing.

Price updates occur for slots labelled A, B, C and F. Slots D and E (79018086, 79018087) were dropped and did not get executed on the chain. The pyth on-chain program guarantees that prices are added to the chain in publish-slot order by publisher.

```
[2021-05-18T22:36:14.048435Z 654359 INF submit price to block-chain             ] symbol=SYMBOL1/USD,price_type=price,price=0.116000,spread=0.001000,slot=79018079,sub_id=1
                                                                                                                                                          ^^ A ^^^
[2021-05-18T22:36:14.237644Z 654359 INF received aggregate price update         ] symbol=SYMBOL1/USD,price_type=price,status=trading,agg_price=0.112000,agg_spread=0.001000,valid_slot=79018076,pub_slot=79018077,my_price=0.112000,my_conf=0.001000,my_status=trading,my_slot=79018075
[2021-05-18T22:36:14.405182Z 654359 INF received aggregate price update         ] symbol=SYMBOL1/USD,price_type=price,status=trading,agg_price=0.113000,agg_spread=0.001000,valid_slot=79018077,pub_slot=79018078,my_price=0.113000,my_conf=0.001000,my_status=trading,my_slot=79018076
[2021-05-18T22:36:16.099126Z 654359 INF submit price to block-chain             ] symbol=SYMBOL1/USD,price_type=price,price=0.117000,spread=0.001000,slot=79018084,sub_id=1
                                                                                                                                                          ^^ B ^^^
[2021-05-18T22:36:16.962077Z 654359 INF submit price to block-chain             ] symbol=SYMBOL1/USD,price_type=price,price=0.118000,spread=0.001000,slot=79018085,sub_id=1
                                                                                                                                                          ^^ C ^^^
[2021-05-18T22:36:17.519741Z 654359 INF submit price to block-chain             ] symbol=SYMBOL1/USD,price_type=price,price=0.119000,spread=0.001000,slot=79018086,sub_id=1
                                                                                                                                                          ^^ D ^^^
[2021-05-18T22:36:17.671924Z 654359 INF received aggregate price update         ] symbol=SYMBOL1/USD,price_type=price,status=trading,agg_price=0.114000,agg_spread=0.001000,valid_slot=79018078,pub_slot=79018079,my_price=0.114000,my_conf=0.001000,my_status=trading,my_slot=79018077
[2021-05-18T22:36:18.109491Z 654359 INF submit price to block-chain             ] symbol=SYMBOL1/USD,price_type=price,price=0.120000,spread=0.001000,slot=79018087,sub_id=1
                                                                                                                                                          ^^ E ^^^
[2021-05-18T22:36:20.537479Z 654359 INF submit price to block-chain             ] symbol=SYMBOL1/USD,price_type=price,price=0.121000,spread=0.001000,slot=79018092,sub_id=1
                                                                                                                                                          ^^ F ^^^
[2021-05-18T22:36:21.195836Z 654359 INF submit price to block-chain             ] symbol=SYMBOL1/USD,price_type=price,price=0.122000,spread=0.001000,slot=79018093,sub_id=1
[2021-05-18T22:36:21.529074Z 654359 INF submit price to block-chain             ] symbol=SYMBOL1/USD,price_type=price,price=0.123000,spread=0.001000,slot=79018094,sub_id=1
[2021-05-18T22:36:21.802004Z 654359 INF received aggregate price update         ] symbol=SYMBOL1/USD,price_type=price,status=trading,agg_price=0.116000,agg_spread=0.001000,valid_slot=79018079,pub_slot=79018085,my_price=0.116000,my_conf=0.001000,my_status=trading,my_slot=79018079
                                                                                                                                                                                                                                                                               ^^ A ^^^
[2021-05-18T22:36:21.969477Z 654359 INF received aggregate price update         ] symbol=SYMBOL1/USD,price_type=price,status=trading,agg_price=0.117000,agg_spread=0.001000,valid_slot=79018085,pub_slot=79018087,my_price=0.117000,my_conf=0.001000,my_status=trading,my_slot=79018084
                                                                                                                                                                                                                                                                               ^^ B ^^^
[2021-05-18T22:36:22.304469Z 654359 INF received aggregate price update         ] symbol=SYMBOL1/USD,price_type=price,status=trading,agg_price=0.118000,agg_spread=0.001000,valid_slot=79018087,pub_slot=79018093,my_price=0.118000,my_conf=0.001000,my_status=trading,my_slot=79018085
                                                                                                                                                                                                                                                                               ^^ C ^^^
[2021-05-18T22:36:22.758348Z 654359 INF submit price to block-chain             ] symbol=SYMBOL1/USD,price_type=price,price=0.125000,spread=0.001000,slot=79018096,sub_id=1
[2021-05-18T22:36:23.121339Z 654359 INF received aggregate price update         ] symbol=SYMBOL1/USD,price_type=price,status=trading,agg_price=0.121000,agg_spread=0.001000,valid_slot=79018093,pub_slot=79018094,my_price=0.121000,my_conf=0.001000,my_status=trading,my_slot=79018092
                                                                                                                                                                                                                                                                               ^^ F ^^^
```
